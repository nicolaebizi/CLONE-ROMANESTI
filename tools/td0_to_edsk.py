#!/usr/bin/env python3
# Teledisk (.TD0) -> EXTENDED CPC DSK (.dsk) converter for CoBra disk images.
#
# Keeps every sector ID (C,H,R,N) in the physical order Teledisk recorded it,
# so interleaved formats such as DEVIL (1,10,2,11,...) are reproduced exactly.
#
#   python3 td0_to_edsk.py GAMES1.TD0 GAMES1.dsk [--max-cyl 80]
#
# Only "TD" (uncompressed) images are handled; "td" images use Teledisk's
# advanced LZ compression and must be expanded first (e.g. with SAMdisk).
import struct
import sys


def rle_decode(data, size):
    # Encoding 2: blocks of [0, n, n literal bytes] or [k, count, 2^k pattern].
    # Some writers use 2*k instead of 2^k for the pattern length; the variant
    # that exactly fills the sector is used.
    for patlen in (lambda k: 1 << k, lambda k: 2 * k):
        out = bytearray()
        i = 0
        ok = True
        while i < len(data) and len(out) < size:
            t = data[i]
            if t == 0:
                n = data[i + 1]
                out += data[i + 2:i + 2 + n]
                i += 2 + n
            else:
                if i + 1 >= len(data):
                    ok = False
                    break
                count = data[i + 1]
                pl = patlen(t)
                pat = data[i + 2:i + 2 + pl]
                out += pat * count
                i += 2 + pl
        if ok and len(out) == size and i == len(data):
            return bytes(out)
    raise ValueError("unrecognised RLE sector encoding")


def read_td0(path, max_cyl):
    d = open(path, 'rb').read()
    sig = d[0:2]
    if sig == b'td':
        sys.exit("%s: advanced compression (td) - expand it first" % path)
    if sig != b'TD':
        sys.exit("%s: not a Teledisk image" % path)
    stepping = d[7]
    p = 12
    if stepping & 0x80:
        clen = struct.unpack_from('<H', d, p + 2)[0]
        p += 10 + clen
    tracks = {}
    while p + 4 <= len(d):
        nsec = d[p]
        if nsec == 0xFF:
            break
        cyl, head = d[p + 1], d[p + 2] & 0x7F
        p += 4
        sectors = []
        truncated = False
        for _ in range(nsec):
            if p + 6 > len(d):
                truncated = True
                break
            c, h, r, n, flags = d[p:p + 5]
            p += 6
            size = 128 << (n & 7)
            data = None
            if not (flags & 0x30):
                blen = struct.unpack_from('<H', d, p)[0]
                enc = d[p + 2]
                raw = d[p + 3:p + 2 + blen]
                p += 2 + blen
                if enc == 0:
                    data = raw[:size]
                elif enc == 1:
                    count, = struct.unpack_from('<H', raw, 0)
                    data = (raw[2:4] * count)[:size]
                elif enc == 2:
                    data = rle_decode(raw, size)
                else:
                    sys.exit("unknown sector encoding %d" % enc)
            st1 = 0x20 if flags & 0x02 else 0       # CRC error -> DE
            st2 = 0x40 if flags & 0x04 else 0       # deleted data -> CM
            if data is None:
                st1 |= 0x01                         # no data field
                data = b''
            elif flags & 0x02:
                st2 |= 0x20                         # DD
            sectors.append((c, h, r, n, st1, st2, data))
        if truncated:
            print("warning: image truncated in cylinder %d head %d" % (cyl, head), file=sys.stderr)
            break
        if max_cyl is None or cyl < max_cyl:
            tracks[(cyl, head)] = sectors
    return tracks


def write_edsk(tracks, path):
    cyls = max(c for c, _ in tracks) + 1
    sides = max(h for _, h in tracks) + 1
    blocks = []
    for c in range(cyls):
        for h in range(sides):
            secs = tracks.get((c, h), [])
            if not secs:
                blocks.append(b'')
                continue
            hdr = bytearray(256)
            hdr[0:12] = b'Track-Info\r\n'
            hdr[0x10] = c
            hdr[0x11] = h
            hdr[0x14] = secs[0][3]
            hdr[0x15] = len(secs)
            hdr[0x16] = 0x4E
            hdr[0x17] = 0xE5
            body = bytearray()
            for i, (sc, sh, sr, sn, st1, st2, data) in enumerate(secs):
                struct.pack_into('<BBBBBBH', hdr, 0x18 + 8 * i, sc, sh, sr, sn, st1, st2, len(data))
                body += data
            blk = bytes(hdr) + bytes(body)
            blk += b'\0' * ((-len(blk)) % 256)
            blocks.append(blk)
    disk = bytearray(256)
    disk[0:34] = b'EXTENDED CPC DSK File\r\nDisk-Info\r\n'
    disk[0x22:0x30] = b'td0_to_edsk   '
    disk[0x30] = cyls
    disk[0x31] = sides
    for i, b in enumerate(blocks):
        disk[0x34 + i] = len(b) // 256
    with open(path, 'wb') as f:
        f.write(disk)
        for b in blocks:
            f.write(b)
    return cyls, sides


def main():
    args = [a for a in sys.argv[1:] if not a.startswith('--')]
    max_cyl = 80
    if '--max-cyl' in sys.argv:
        max_cyl = int(sys.argv[sys.argv.index('--max-cyl') + 1])
        args = [a for a in args if a != str(max_cyl)]
    if len(args) != 2:
        sys.exit(__doc__ if __doc__ else "usage: td0_to_edsk.py in.TD0 out.dsk")
    tracks = read_td0(args[0], max_cyl)
    cyls, sides = write_edsk(tracks, args[1])
    first = tracks[min(tracks)]
    print("%s: %d cylinders x %d sides, track 0: %d sectors N=%d, IDs %s" % (
        args[1], cyls, sides, len(first), first[0][3], [s[2] for s in first]))


if __name__ == '__main__':
    main()
