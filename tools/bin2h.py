# bin2h.py <in.bin> <out.h> <symbol> <size> <title>
# Pads the image with 0xFF (unprogrammed EPROM) up to <size> bytes.
import sys
src, dst, sym, size, title = sys.argv[1], sys.argv[2], sys.argv[3], int(sys.argv[4], 0), sys.argv[5]
d = open(src, 'rb').read()
if len(d) > size: sys.exit('image larger than %d' % size)
d += b'\xff' * (size - len(d))
with open(dst, 'w') as f:
    f.write('// %s\n// Source image: %s (padded with 0xFF to %d bytes)\n#pragma once\n\n' % (title, src.split('/')[-1], size))
    f.write('static const unsigned char %s[%d] = {\n' % (sym, size))
    for i in range(0, size, 16):
        f.write('  ' + ', '.join('0x%02X' % b for b in d[i:i+16]) + ',\n')
    f.write('};\n')
