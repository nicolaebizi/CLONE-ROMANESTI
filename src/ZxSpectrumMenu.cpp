// SPDX-License-Identifier: MIT
// Copyright (c) 2026 fruit-bat
#include <algorithm>
#include "pico/bootrom.h"
#include "ZxSpectrumMenu.h"
#include "ZxSpectrum.h"
#include "PicoPen.h"
#include "FatFsSpiDirReader.h"
#include "FatFsSpiInputStream.h"
#include "BufferedInputStream.h"
#include "hardware/clocks.h"
#include "ff.h"
#include "ZxSpectrumVoltage.h"
#include "ZxSpectrumAudio.h"
#include "ZxSpectrumAudioVol.h"
#include "ZxSpectrumAudioDriver.h"
#include "ZxSpectrumVideoDriver.h"
#include "PicoCoreVoltage.h"
#include "CobraDiskFile.h"
#if PICO_RP2350
#include "BootSwap.h"
#endif

// #define DEBUG_ZX_MENU

#define ZX_VERSION_MAJOR 0
#define ZX_VERSION_MINOR 36

#ifdef DEBUG_ZX_MENU
#define DBG_PRINTF(...) printf(__VA_ARGS__)
#else
#define DBG_PRINTF(...)
#endif

#ifndef SZ_WIZ_ML
#define SZ_WIZ_ML 3
#endif
#ifndef SZ_WIZ_CW1
#define SZ_WIZ_CW1 16
#endif
#ifndef SZ_WIZ_CW2
#define SZ_WIZ_CW2 40
#endif
#ifndef SZ_FRAME_COLS
#define SZ_FRAME_COLS 80
#endif
#ifndef SZ_FRAME_ROWS
#define SZ_FRAME_ROWS 30
#endif
#ifndef SZ_FRAME_X
#define SZ_FRAME_X 0
#endif
#ifndef SZ_FRAME_Y
#define SZ_FRAME_Y 0
#endif
#ifndef SZ_MENU_SEP
#define SZ_MENU_SEP 1
#endif
#ifndef SZ_FILE_SEP
#define SZ_FILE_SEP 1
#endif
#ifndef SZ_FILE_ROWS
#define SZ_FILE_ROWS 18
#endif

#define SZ_WIZ_COLS (SZ_FRAME_COLS-(2*SZ_WIZ_ML))

static const char *fext(const char *filename) {
  const char *dot = strrchr(filename, '.');
  if(!dot || dot == filename) return "";
  return dot + 1;
}

// Custom 48K ROM loader: only 16384-byte *.rom images are accepted
// (extension match is case-insensitive).
static bool isCustom48kRom(uint32_t size, const char *filename) {
  const char *ext = fext(filename);
  const char *r = "rom";
  while (*r) {
    if ((*ext | 0x20) != *r) return false; // case-insensitive compare
    ++r; ++ext;
  }
  return *ext == 0 && size == 16384;
}

PicoQuickKeyAscii *ZxSpectrumMenu::getQuickKey(uint i) {
  switch(i) {
    case 0: return &_k1;
    case 1: return &_k2;
    case 2: return &_k3;
    case 3: return &_k4;
    case 4: return &_k5;
    case 5: return &_k6;
    case 6: return &_k7;
    case 7: return &_k8;
    case 8: return &_k9;
    default: return &_k1;
  }
}

void ZxSpectrumMenu::cubzMount(uint8_t drive, FILINFO *finfo, const char *path) {
  _zxSpectrum->cubzEjectDisk(drive);
  if (_cubzDiskFile[drive]) {
    delete _cubzDiskFile[drive];
    _cubzDiskFile[drive] = 0;
  }
  CobraDiskFile *f = new CobraDiskFile(_sdCard, path);
  if (!f->isOpen() || !_zxSpectrum->cubzInsertDisk(drive, f)) {
    delete f;
    _cubzDiskName[drive] = "? ";
    _cubzDiskName[drive] += finfo->fname;
    return;
  }
  _cubzDiskFile[drive] = f;
  _cubzDiskName[drive] = finfo->fname;
  if (!f->writable()) _cubzDiskName[drive] += " (R/O)";
}

void ZxSpectrumMenu::cobraEject(uint8_t drive) {
  _zxSpectrum->cobraEjectDisk(drive);
  if (_cobraDiskFile[drive]) {
    delete _cobraDiskFile[drive];
    _cobraDiskFile[drive] = 0;
  }
  _cobraDiskName[drive].clear();
}

void ZxSpectrumMenu::setWizLayout(int32_t margin, int32_t cols1, int32_t cols2, int32_t w) {
   _wizCol1Width = cols1;
   _wizCol2Width = cols2;
   _wizLeftMargin = margin;
   _wizCols = ww() - (2 * margin);
   _wiz.move(_wizLeftMargin, _wiz.wy(), _wizCols, _wiz.wh());
   _main.move(0, 0, _wizCols, _main.wh());
   _tapePlayer.move(0, 0, _wizCols, _tapePlayer.wh());
    _chooseTape.move(0, 0, _wizCols, _chooseTape.wh());
    _chooseSnap.move(0, 0, _wizCols, _chooseSnap.wh());
    _hcDisk.move(0, 0, _wizCols, _hcDisk.wh());
    _chooseHcDiskA.move(0, 0, _wizCols, _chooseHcDiskA.wh());
    _chooseBasicDiskA.move(0, 0, _wizCols, _chooseBasicDiskA.wh());
    _chooseHcDiskB.move(0, 0, _wizCols, _chooseHcDiskB.wh());
#ifdef ZX_PLUS3_ROM_AVAILABLE
    _choosePlus3DiskA.move(0, 0, _wizCols, _choosePlus3DiskA.wh());
    _choosePlus3DiskB.move(0, 0, _wizCols, _choosePlus3DiskB.wh());
#endif
#if PICO_RP2350
    _chooseTimsDiskA.move(0, 0, _wizCols, _chooseTimsDiskA.wh());
    _chooseTimsDiskB.move(0, 0, _wizCols, _chooseTimsDiskB.wh());
#endif
    _chooseHc88DiskA.move(0, 0, _wizCols, _chooseHc88DiskA.wh());
    _chooseHc88DiskB.move(0, 0, _wizCols, _chooseHc88DiskB.wh());
    _chooseZx48kRom.move(0, 0, _wizCols, _chooseZx48kRom.wh());
    _reset.move(0, 0, _wizCols, _reset.wh());
   _cobra.move(0, 0, _wizCols, _cobra.wh());
   _chooseCobraDisk.move(0, 0, _wizCols, _chooseCobraDisk.wh());
   _chooseCubzDiskA.move(0, 0, _wizCols, _chooseCubzDiskA.wh());
   _chooseCubzDiskB.move(0, 0, _wizCols, _chooseCubzDiskB.wh());
   _joystick.move(0, 0, _wizCols, _joystick.wh());
   _mouse.move(0, 0, _wizCols, _mouse.wh());
   _devices.move(0, 2, _wizCols, _devices.wh());
   _tzxSelect.move(0, 0, _wizCols, _tzxSelect.wh());
   _keyboard.move(0 , 0, w, 10),
   repaint();
}

ZxSpectrumMenu::ZxSpectrumMenu(
    SdCardFatFsSpi* sdCard,
    ZxSpectrum *zxSpectrum,
    ZxSpectrumSettings *settings
) :
 PicoWin(SZ_FRAME_X, SZ_FRAME_Y, SZ_FRAME_COLS, SZ_FRAME_ROWS),
   _wizCol1Width(SZ_WIZ_CW1),
   _wizCol2Width(SZ_WIZ_CW2),
   _wizLeftMargin(SZ_WIZ_ML),
   _wizCols(SZ_WIZ_COLS),
   _menuRowsPerItem(SZ_MENU_SEP),
   _explorerRowsPerFile(SZ_FILE_SEP),
   _explorerRows(SZ_FILE_ROWS),
   _videoDriverIndex(0),
   _pathZxSpectrum("/zxspectrum"),
   _pathSnaps(&_pathZxSpectrum, "snapshots"),
    _pathTapes(&_pathZxSpectrum, "tapes"),
    _pathQuickSaves(&_pathSnaps, "quicksaves"),
    // Custom 48K ROM images folder (main menu option R)
    _pathRoms(&_pathZxSpectrum, "roms"),
    _pathDisks(&_pathZxSpectrum, "disks"),
    _pathCobraDisks(&_pathDisks, "cobra1"),
    _pathDisksHc2000(&_pathDisks, "hc2000"),
    _pathDisksBasic(&_pathDisks, "basic"),
#ifdef ZX_PLUS3_ROM_AVAILABLE
    _pathDisksPlus3(&_pathDisks, "plus3"),
#endif
  _sdCard(sdCard),
  _zxSpectrum(zxSpectrum),
  _zxSpectrumSettings(settings),
  _tis(0),

  _k1('1'), _k2('2'), _k3('3'), _k4('4'), _k5('5'), _k6('6'), _k7('7'), _k8('8'), _k9('9'), 
  _kV('v'), _kK('k'), _kS('s'), _kQ('q'), _kC('c'), _kD('D'),
  // Disk menu (D) rows 10-13: the CUB-Z / HC88 rows reuse the shared letter
  // quick keys (_kC/_kD are the main-menu CoBra / Disk keys - quick keys are
  // matched per menu, exactly like the shared _k1.._k9).
  _kA('A'), _kB('B'),
  // Main menu option R: custom 48K ROM (no other quick key uses 'R')
  _kR('R'),

  _wiz(_wizLeftMargin, 6, _wizCols, _explorerRows * _explorerRowsPerFile),
  _wizUtils(&_wiz, _explorerRowsPerFile, &_k1, &_k2),
  _main(0, 0, _wizCols, 15, _menuRowsPerItem),
  _audioOp("Audio"),
#ifdef ZX_PLUS3_ROM_AVAILABLE
  _mountPlus3BOp("Mount +3 B"),
#endif
  _tapePlayer(0, 0, _wizCols, 6, _menuRowsPerItem),

  _chooseTapeOp("Tapes"),
  _ejectTapeOp("Eject tape"),
  _chooseTape(&_wizUtils, sdCard, &_pathTapes, 0, 0, _wizCols, _explorerRows, _explorerRowsPerFile),
  _chooseSnap(&_wizUtils, sdCard, &_pathSnaps, 0, 0, _wizCols, _explorerRows, _explorerRowsPerFile),

  _hcDisk(0, 0, _wizCols,
#if PICO_RP2350
    // Rows for the RP2350-only TIM S Plus mount entry and the two CUB-Z
    // drives (plus the HC88 rows present on every platform)
    13,
#else
    // HC88 + CUB-Z mount rows after the HC2000/+3 rows
    10,
#endif
    _menuRowsPerItem),
  _hcDiskOp("HC2K,+3,TIMS PLUS,CUBZ,HC88"),
  _hcDiskAOp("Mount HC2000 drive A"),
  _hcBasicDiskAOp("Mount BASIC disk A"),
  _hcDiskBOp("Mount HC2000 drive B"),
  _hcDiskEjectOp("Eject disks"),
#ifdef ZX_PLUS3_ROM_AVAILABLE
  _plus3DiskAOp("Mount +3 drive A"),
  _plus3DiskBOp("Mount +3 drive B"),
#endif
  _chooseHcDiskA(&_wizUtils, sdCard, &_pathDisksHc2000, 0, 0, _wizCols, _explorerRows, _explorerRowsPerFile),
  _chooseBasicDiskA(&_wizUtils, sdCard, &_pathDisksBasic, 0, 0, _wizCols, _explorerRows, _explorerRowsPerFile),
#ifdef ZX_PLUS3_ROM_AVAILABLE
  _choosePlus3DiskA(&_wizUtils, sdCard, &_pathDisksPlus3, 0, 0, _wizCols, _explorerRows, _explorerRowsPerFile),
  _choosePlus3DiskB(&_wizUtils, sdCard, &_pathDisksPlus3, 0, 0, _wizCols, _explorerRows, _explorerRowsPerFile),
#endif
  _chooseHcDiskB(&_wizUtils, sdCard, &_pathDisksHc2000, 0, 0, _wizCols, _explorerRows, _explorerRowsPerFile),
  _hdisA(0),
  _hdisB(0),
  _hcMountA(-1),
  _hcMountB(-1),
#if PICO_RP2350
  // TIM S Plus drive mount (disk menu quick keys 8/9; RP2350 only):
  // dedicated image folder zxspectrum/disks/TIMS PLUS (NOT the hc2000 dir).
  _pathDisksTims(&_pathDisks, "TIMS PLUS"),
  _timsDiskAOp("Mount TIMS PLUS drive A"),
  _timsDiskBOp("TIMS PLUS drive B"),
  _chooseTimsDiskA(&_wizUtils, sdCard, &_pathDisksTims, 0, 0, _wizCols, _explorerRows, _explorerRowsPerFile),
  _chooseTimsDiskB(&_wizUtils, sdCard, &_pathDisksTims, 0, 0, _wizCols, _explorerRows, _explorerRowsPerFile),
  _timsA(0),
  _timsB(0),
  _timsMountA(-1),
  _timsMountB(-1),
#endif
  // ICE Felix HC88 disk chooser (same chooser machinery as TIMS PLUS):
  // dedicated SD folder zxspectrum/disks/HC88 (auto-created
  // at menu init). Phase 2 mounts into hcFdc() drives 0/1.
  _pathDisksHc88(&_pathDisks, "HC88"),
  _hc88DiskAOp("Mount HC88 drive A"),
  _hc88DiskBOp("Mount HC88 drive B"),
  _chooseHc88DiskA(&_wizUtils, sdCard, &_pathDisksHc88, 0, 0, _wizCols, _explorerRows, _explorerRowsPerFile),
  _chooseHc88DiskB(&_wizUtils, sdCard, &_pathDisksHc88, 0, 0, _wizCols, _explorerRows, _explorerRowsPerFile),
  _hc88A(0),
  _hc88B(0),
  _hc88MountA(-1),
  _hc88MountB(-1),
  _pathDisksCubz(&_pathDisks, "CUBZ"),
  _cubzDiskAOp("CUB-Z drive A"),
  _cubzDiskBOp("CUB-Z drive B"),
  _chooseCubzDiskA(&_wizUtils, sdCard, &_pathDisksCubz, 0, 0, _wizCols, _explorerRows, _explorerRowsPerFile),
  _chooseCubzDiskB(&_wizUtils, sdCard, &_pathDisksCubz, 0, 0, _wizCols, _explorerRows, _explorerRowsPerFile),
  _cubzDiskFile{0, 0},

  // Custom 48K ZX Spectrum ROM chooser (main menu option R): the same
  // chooser machinery as the HC88/TIMS choosers, pointed at the dedicated
  // folder zxspectrum/roms (auto-created at menu init). The selected
  // 16384-byte .rom image is copied into the ZxSpectrum custom-ROM RAM
  // buffer; the 48K machine then fetches it instead of the flash ROM.
  _zx48kRomOp("ZX48K Custom ROM"),
  _chooseZx48kRom(&_wizUtils, sdCard, &_pathRoms, 0, 0, _wizCols, _explorerRows, _explorerRowsPerFile),

  _audioOptions(0, 0, _wizCols, 6, _menuRowsPerItem),

  _reset(0, 0, _wizCols,
#if PICO_RP2350
    // One extra row for the RP2350-only TIM S Plus reset entry
    // (the list scrolls anyway; HC88 is present on every platform)
    9 * SZ_MENU_SEP,
#else
    // 48K/128K/Turbo/HC2000/CoBra/+3/CUB-Z/HC88
    8 * SZ_MENU_SEP,
#endif
    _menuRowsPerItem),
  _reset48kOp("Reset 48K ZX Spectrum"),
  _reset128kOp("Reset 128K ZX Spectrum"),
  _resetTurboOp("Reset TURBO-SPECTRUM"),
  _resetHc2000Op("Reset HC2000"),
  _resetCobraOp("Reset CoBra 1"),
#if PICO_RP2350
  _resetTimsOp("Reset TIM S Plus"),
#endif
  _resetHc88Op("Reset HC88"),
  _resetCubzOp("Reset CUB-Z"),
#ifdef ZX_PLUS3_ROM_AVAILABLE
  _resetPlus3Op("Reset ZX Spectrum +3"),
#endif

  _cobra(0, 0, _wizCols, 9, _menuRowsPerItem),
  _cobraEjectOp("Eject all disks"),
  _cobraResetBasicOp("RESET BASIC (K1)"),
  _chooseCobraDisk(&_wizUtils, sdCard, &_pathCobraDisks, 0, 0, _wizCols, _explorerRows, _explorerRowsPerFile),
  _cobraDrive(0),
  _cobraDiskFile{0, 0, 0, 0},
  
  _joystick(0, 0, _wizCols, 6, _menuRowsPerItem),
  _joystickKempstonOp("Kempston"),
  _joystickSinclairLROp("Sinclair L+R"),
  _joystickSinclairRLOp("Sinclair R+L"),

  _mouse(0, 0, _wizCols, 6, _menuRowsPerItem),
  _mouseKempstonMouseOp("Kempston Mouse"),
  _mouseKempstonJoystickOp("Kemptson Joystick"),
  _mouseSinclairJoystickLOp("Sinclair Joystick L"),
  _mouseSinclairJoystickROp("Sinclair Joystick R"),

  _audioOutOptions(0, 0, _wizCols, 6, _menuRowsPerItem),
  _audioNullOp(_zx_spectrum_audio_drivers[0].name),
  _audioPioPwmOp(_zx_spectrum_audio_drivers[1].name),
  _audioPwmOp(_zx_spectrum_audio_drivers[2].name),
  _audioI2sOp(_zx_spectrum_audio_drivers[3].name),
  _audioHdmiOp(_zx_spectrum_audio_drivers[4].name), 

  _videoOp("Video"),
  _videoOptions(0, 0, _wizCols, 6, _menuRowsPerItem),
  _videoVgaOp("VGA"),
  _videoDviOp("DVI"),
  _videoLcdOp("LCD"),

  _settings(0, 0, _wizCols, 6, _menuRowsPerItem),
  _settingsSaveOp("Save"),
  _settingsLoadOp("Load"),

  _system(0, 0, _wizCols, 6, _menuRowsPerItem),
  _systemBootSelOp("Boot select"),

  _volume(0, 0, 16, 16),
  _keyboard(((SZ_FRAME_COLS-44)/2)-2 , 0, 52, 10),

  _devices(0, 2, _wizCols, 2),
 
  _tzxSelect(0, 0, _wizCols, 6, _menuRowsPerItem)
{ 
  _tzxSelect.onToggle(
    [=](PicoOption *option, int32_t i) {
      _wiz.pop(true);
      if (_tzxOption) _tzxOption(i);
    }
  );
  
  addChild(&_devices, false);
  _devices.onPaint([=](PicoPen *pen){
    int32_t jl = (_zxSpectrum->joystick()->isConnectedL() ? 1 : 0) + (_zxSpectrum->mouse()->isConnectedL() ? 1 : 0);
    int32_t jr = (_zxSpectrum->joystick()->isConnectedR() ? 1 : 0) + (_zxSpectrum->mouse()->isConnectedR() ? 1 : 0);
    bool k1 = _zxSpectrum->keyboard1() && _zxSpectrum->keyboard1()->isMounted();
    bool k2 = _zxSpectrum->keyboard2() && _zxSpectrum->keyboard2()->isMounted();
    bool m = _zxSpectrum->mouse() && _zxSpectrum->mouse()->isMounted();
    const float vsys = read_voltage_sensor();
    const float vcore = pico_get_core_voltage();
    const float fmhz = (float)clock_get_hz(clk_sys) / 1000000.0;

    pen->printAtF(0, 0, false, 
        "Clk: %3.1fMhz Vcore:%1.2fv Vsys:%1.2fv",
        fmhz,
        vcore,
        vsys
        );

    pen->printAtF(0, 1, false,"USB: Joystick%s %s%s%s, Keyboard%s %s%s%s, Mice: %s",
       (jl + jr ? "s" : ""),
       (jl ? "L" : ""), (!jl && !jr ? "0" : (!!jl & !!jr ? "&" : "")), (jr ? "R" : ""),
       (k1 == k2 ? "s" : ""),
       (k1 ? "1" : ""), (!k1 && !k2 ? "0" : (k1 & k2 ? "&" : "")), (k2 ? "2" : ""),
       (m ? "1" : "0")
       );
    _devices.repaint();
  });
  
  addChild(&_wiz, true);
  _wiz.push(&_main, [](PicoPen *pen){ pen->printAt(0, 0, false, "Main menu"); }, true);
  _wiz.onKeydown([=](uint8_t keycode, uint8_t modifiers, uint8_t ascii) {
     // Escape or Left for back
     return (ascii != 27 && ascii != 129) || !_wiz.pop(true);
   });
   
  _main.addOption(_snapOp.addQuickKey(&_k1));
  _main.addOption(_tapePlayerOp.addQuickKey(&_k2));
  _main.addOption(_freqOp.addQuickKey(&_k3));
  _main.addOption(_intSourceOp.addQuickKey(&_k4));
  _main.addOption(_audioOp.addQuickKey(&_k5));
  _main.addOption(_resetOp.addQuickKey(&_k6));
  // +3 drive B is mounted from the disk menu (D); k7 is the joystick again.
  _main.addOption(_joystickOp.addQuickKey(&_k7));
  _main.addOption(_mouseOp.addQuickKey(&_k8));
  _main.addOption(_settingsOp.addQuickKey(&_k9));
  _main.addOption(_keyboardOp.addQuickKey(&_kK));
  _main.addOption(_systemOp.addQuickKey(&_kS));
  _main.addOption(_quickSaveOp.addQuickKey(&_kQ));
  _main.addOption(_cobraOp.addQuickKey(&_kC));

  _main.enableQuickKeys();
  _snapOp.onPaint([=](PicoPen *pen){
    pen->clear();
    pen->printAtF(0, 0, false,"%-*s[ %-*s]", _wizCol1Width, "Snapshot", _wizCol2Width, _snapName.c_str());
  });
  _snapOp.toggle([=]() {
    _wiz.push(
      &_chooseSnap, 
      [=](PicoPen *pen){ 
        pen->printAtF(0, 0, false, "%s  [%s]", "Snaps", 
          SZ_FRAME_COLS < 50 ? "1=DE 2=RN 3=CP 4=PA 5=RF 6=SA" : "1=DEL 2=REN 3=CPY 4=PST 5=REF 6=SAV");
      },
      true);
  });
  _tapePlayerOp.onPaint([=](PicoPen *pen){
    pen->clear();
    pen->printAtF(0, 0, false,"%-*s[ %-*s]", _wizCol1Width, "Tape player", _wizCol2Width, _tapeName.c_str());
  });
  _tapePlayerOp.toggle([=]() {
    _wiz.push(
      &_tapePlayer, 
      [=](PicoPen *pen){ 
        pen->printAtF(0, 0, false,"Tape player [ %-*s]", _wizCol2Width, _tapeName.c_str());
      }, 
      true);
  });
  _freqOp.toggle([=]() {
    _zxSpectrum->toggleModerate();
    _main.repaint();
  });
  _freqOp.onPaint([=](PicoPen *pen){
    pen->clear();
    const char *m;
    switch(_zxSpectrum->moderate()) {
      case 9: m = "3.5 Mhz" ; break;
      case 13: m = "2.5 Mhz" ; break;
      case 8: m = "4.0 Mhz" ; break;
      case 0: m = "Unmoderated" ; break;
      default: m = "Unknown" ; break;
    }
    pen->printAtF(0, 0, false,"%-*s[ %-*s]", _wizCol1Width, "CPU Speed", _wizCol2Width, m);
  });
  _intSourceOp.toggle([=]() {
    _zxSpectrum->toggleIntSource();
    _main.repaint();
  });
  _intSourceOp.onPaint([=](PicoPen *pen){
    pen->clear();
    const char *m;
    switch(_zxSpectrum->intSource()) {
      case SyncToCpu: m = "Count CPU cycles" ; break;
      case SyncToDisplay: m = "Sync to display" ; break;
      default: m = "Unknown" ; break;
    }
    pen->printAtF(0, 0, false,"%-*s[ %-*s]", _wizCol1Width, "CPU Interrupt", _wizCol2Width, m);
  });
  _audioOp.toggle([=]() {
    _wiz.push(
      &_audioOptions, 
      [=](PicoPen *pen){ 
        pen->printAtF(0, 0, false,"Audio options");
      }, 
      true);
  });
  _resetOp.onPaint([=](PicoPen *pen){
    pen->clear();
    const char *m;
    switch(_zxSpectrum->type()) {
      case ZxSpectrum48k: m = "48k ZX Spectrum" ; break;
      case ZxSpectrum128k: m = "128k ZX Spectrum" ; break;
      case ZxSpectrumTurbo: m = "TURBO-SPECTRUM" ; break;
      case ZxSpectrumHc2000: m = "HC2000" ; break;
      case ZxSpectrumCobra: m = "CoBra 1" ; break;
      case ZxSpectrumPlus3: m = "ZX Spectrum +3" ; break;
#if PICO_RP2350
      case ZxSpectrumTimsPlus: m = "TIM S Plus" ; break;
#endif
      case ZxSpectrumHc88: m = "HC88" ; break;
      case ZxSpectrumCubz: m = "CUB-Z" ; break;
      default: m = "" ; break;
    }
    pen->printAtF(0, 0, false,"%-*s[ %-*s]", _wizCol1Width, "Reset", _wizCol2Width, m);
  });
  _resetOp.toggle([=]() {
    _wiz.push(
      &_reset, 
      [](PicoPen *pen){ pen->printAt(0, 0, false, "Reset options"); }, 
      true);
  });

  // ZX Spectrum +3: k7 on the main menu opens the +3 drive B chooser directly.
#ifdef ZX_PLUS3_ROM_AVAILABLE
  _mountPlus3BOp.onPaint([=](PicoPen *pen){
    pen->clear();
    pen->printAtF(0, 0, false, "%-*s[ %-*s]", _wizCol1Width, "Mount +3 B", _wizCol2Width, _hcDiskNameB.c_str());
  });
  _mountPlus3BOp.toggle([=]() {
    _choosePlus3DiskB.reload();
    _wiz.push(
      &_choosePlus3DiskB,
      [=](PicoPen *pen){ pen->printAt(0, 0, false, "+3 disk images (B:)"); },
      true);
  });
#endif

  _settings.addOption(_settingsSaveOp.addQuickKey(&_k1));
  _settings.addOption(_settingsLoadOp.addQuickKey(&_k2));
  _settings.addOption(_videoOp.addQuickKey(&_k3));
  _settings.enableQuickKeys();
  _settingsSaveOp.toggle([=]() {
    saveSettings();
    _wiz.pop(true);
  });
  _settingsLoadOp.toggle([=]() {
    loadSettings();
    _wiz.pop(true);
  });
  _settingsOp.onPaint([=](PicoPen *pen){
    pen->clear();
    pen->printAtF(0, 0, false,"%-*s", _wizCol1Width, "Settings");
  });  
  _settingsOp.toggle([=]() {
    _wiz.push(
      &_settings, 
      [](PicoPen *pen){ pen->printAt(0, 0, false, "Settings"); }, 
      true);
  });

  _system.addOption(_systemBootSelOp.addQuickKey(&_k1));
  _system.enableQuickKeys();
  _systemOp.onPaint([=](PicoPen *pen){
    pen->clear();
    pen->printAtF(0, 0, false,"%-*s", _wizCol1Width, "System");
  });  
  _systemOp.toggle([=]() {
    _wiz.push(
      &_system, 
      [](PicoPen *pen){ pen->printAt(0, 0, false, "System"); }, 
      true);
  });

  _quickSaveOp.onPaint([=](PicoPen *pen){
    pen->clear();
    pen->printAtF(0, 0, false,"%-*s", _wizCol1Width, "Quick save");
  });  
  _quickSaveOp.toggle([=]() {
    quickSave(0);
    if (_snapLoadedListener) _snapLoadedListener("QS1");
  });

  _systemBootSelOp.toggle([=]() {
    _wizUtils.confirm(
      [=](PicoPen *pen){
        pen->printAtF(0, 0, false, "Restart Pico into boot loader?");
      },
      [=]() {
        reset_usb_boot(0,0);
      }
    );
  });

  _keyboard.config(
      [this](uint8_t line, uint8_t key) { _zxSpectrum->keyboard1()->virtuallpress(line,key); }
    );
 
  _keyboardOp.onPaint([=](PicoPen *pen){
    pen->clear();
    pen->printAtF(0, 0, false,"(OSK) Keyboard");
  });
  _keyboardOp.toggle([=]() {
    _wiz.push(
      &_keyboard, 
      [](PicoPen *pen){ pen->printAt(0, 0, false, "On Screen Keyboard"); }, 
      true);
  });


  _joystickOp.onPaint([=](PicoPen *pen){
    pen->clear();
    const char *m = "N/A";
    if (_zxSpectrum->joystick()) {
      switch(_zxSpectrum->joystick()->mode()) {
        case ZxSpectrumJoystickModeKempston: m = "Kempston" ; break;
        case ZxSpectrumJoystickModeSinclairLR: m = "Sinclair L+R" ; break;
        case ZxSpectrumJoystickModeSinclairRL: m = "Sinclair R+L" ; break;
        default: m = "N/A" ; break;
      }
    }
    pen->printAtF(0, 0, false,"%-*s[ %-*s]", _wizCol1Width, "Joystick", _wizCol2Width, m);
  });
  _joystickOp.toggle([=]() {
    _wiz.push(
      &_joystick, 
      [](PicoPen *pen){ pen->printAt(0, 0, false, "Joystick mode"); }, 
      true);
  });

  _mouseOp.onPaint([=](PicoPen *pen){
    pen->clear();
    const char *m = "N/A";
    if (_zxSpectrum->mouse()) {
      switch(_zxSpectrum->mouse()->mouseMode()) {
        case ZxSpectrumMouseModeKempstonMouse: m = "Kempston Mouse" ; break;
        case ZxSpectrumMouseModeJoystick: {
          switch(_zxSpectrum->mouse()->mode()) {
            case ZxSpectrumJoystickModeKempston: m = "Kempston Joystick" ; break;
            case ZxSpectrumJoystickModeSinclairLR: m = "Sinclair Joystick L" ; break;
            case ZxSpectrumJoystickModeSinclairRL: m = "Sinclair Joystick R" ; break;
            default: m = "N/A" ; break;
          }
          break;     
        }
        default: m = "N/A" ; break;
      }
    }
    pen->printAtF(0, 0, false,"%-*s[ %-*s]", _wizCol1Width, "Mouse", _wizCol2Width, m);
  });
  _mouseOp.toggle([=]() {
    _wiz.push(
      &_mouse, 
      [](PicoPen *pen){ pen->printAt(0, 0, false, "Mouse mode"); }, 
      true);
  });

  _chooseSnap.onToggle = [=](FILINFO *finfo, int32_t i, const char* path) {
    snapName(finfo->fname);
    FatFsSpiInputStream *is = new FatFsSpiInputStream(_sdCard, path);
    _zxSpectrum->loadZ80(is);
    delete is;
    if (_snapLoadedListener) _snapLoadedListener(finfo->fname);
  };
  _chooseSnap.onWizSaveFile = [=](FatFsSpiOutputStream *os) {
    return _zxSpectrum->saveZ80(os);
  };

  // Start of audio settings
  uint audioQuickKeyIndex = 0;
  _audioOptions.addOption(_muteOp.addQuickKey(getQuickKey(audioQuickKeyIndex++)));
  #ifndef BZR_PIN  
  _audioOptions.addOption(_volumeOp.addQuickKey(getQuickKey(audioQuickKeyIndex++)));
  #endif
  _audioOptions.addOption(_audioOutOp.addQuickKey(getQuickKey(audioQuickKeyIndex++)));
  _audioOptions.enableQuickKeys();

  _muteOp.toggle([=]() {
    _zxSpectrum->toggleMute();
    _main.repaint();
  });
  _muteOp.onPaint([=](PicoPen *pen){
    pen->clear();
    pen->printAtF(0, 0, false,"%-*s[ %-*s]", _wizCol1Width, "Audio", _wizCol2Width, _zxSpectrum->mute() ? "Off" : "On");
  });
  _volume.config(
    [](uint32_t v) { zxSpectrumAudioSetVolume(v); },
    []() { return zxSpectrumAudioGetVolume(); }
  );
  _volumeOp.onPaint([=](PicoPen *pen){
    pen->clear();
    pen->printAtF(0, 0, false,"%-*s[ %-*s]", _wizCol1Width, "Volume", _wizCol2Width, _volume.getChars());
  });
  _volumeOp.toggle([=]() {
    _wiz.push(
      &_volume, 
      [](PicoPen *pen){ pen->printAt(0, 0, false, "Volume control"); }, 
      true);
  });
  _audioOutOp.onPaint([=](PicoPen *pen){
    pen->clear();
    const char *m = _zxSpectrum->getAudioDriver()->name;
    pen->printAtF(0, 0, false,"%-*s[ %-*s]", _wizCol1Width, "Audio out", _wizCol2Width, m);
  });
  _audioOutOp.toggle([=]() {
    const char *m = _zxSpectrum->getAudioDriver()->name;
    _wiz.push(
      &_audioOutOptions, 
      [=](PicoPen *pen){ 
        pen->printAtF(0, 0, false, "Audio output [%s]", m); 
      }, 
      true);
  });
// End of audio settings


  _reset.addOption(_reset48kOp.addQuickKey(&_k1));
  _reset.addOption(_reset128kOp.addQuickKey(&_k2));
  _reset.addOption(_resetTurboOp.addQuickKey(&_k3));
  _reset.addOption(_resetHc2000Op.addQuickKey(&_k4));
  _reset.addOption(_resetCobraOp.addQuickKey(&_k5));
#ifdef ZX_PLUS3_ROM_AVAILABLE
  // Verified lesson (LESSONS.md): reset submenu key k6 resets ZX Spectrum +3.
  _reset.addOption(_resetPlus3Op.addQuickKey(&_k6));
#endif
#if PICO_RP2350
  // TIM S Plus (boot-only, RP2350 only): the free quick key in the reset
  // submenu (k1-k5/k6 +3 already taken; the entry scrolls into view).
  // Disk mounting is NOT here: it lives in the HC2000 disk menu below.
  _reset.addOption(_resetTimsOp.addQuickKey(&_k7));
#endif
  _reset.addOption(_resetCubzOp.addQuickKey(&_k8));
  // ICE Felix HC88: after the CUB-Z entry, quick key k9. Phase 1 is machine
  // type + memory map + VRAM video only; disk mounts live in the HC2000
  // disk menu below.
  _reset.addOption(_resetHc88Op.addQuickKey(&_k9));
  _resetCubzOp.toggle([=]() {
    _snapName.clear();
    _zxSpectrum->reset(ZxSpectrumCubz);
    _wiz.pop(true);
  });
  _reset.enableQuickKeys();

  _resetTurboOp.toggle([=]() {
    _snapName.clear();
    _zxSpectrum->reset(ZxSpectrumTurbo);
    _wiz.pop(true);
  });

  _resetHc2000Op.toggle([=]() {
    _snapName.clear();
    _zxSpectrum->reset(ZxSpectrumHc2000);
    _wiz.pop(true);
  });

  _resetCobraOp.toggle([=]() {
    _snapName.clear();
    _zxSpectrum->reset(ZxSpectrumCobra);
    _wiz.pop(true);
  });

#if PICO_RP2350
  _resetTimsOp.toggle([=]() {
    _snapName.clear();
    _zxSpectrum->reset(ZxSpectrumTimsPlus);
    _wiz.pop(true);
  });
#endif

  _resetHc88Op.toggle([=]() {
    _snapName.clear();
    _zxSpectrum->reset(ZxSpectrumHc88);
    _wiz.pop(true);
  });

#if PICO_RP2350
  // TIM S Plus disk mounts (phase 2, RP2350 only): the same SD chooser
  // machinery as the HC2000 chooser, but pointed at the dedicated folder
  // zxspectrum/disks/TIMS PLUS (created at menu init). Drive A = FDC
  // unit 0, drive B = FDC unit 1 (fuse tims_fdc.c). The row label shows
  // the mounted file name, HC2000 style.
  _timsDiskAOp.onPaint([=](PicoPen *pen){
    pen->clear();
    pen->printAtF(0, 0, false,"%-*s[ %-*s]", _wizCol1Width, "TIMS PLUS drive A",
      _wizCol2Width, _timsDiskNameA.empty() ? "-" : _timsDiskNameA.c_str());
  });
  _timsDiskBOp.onPaint([=](PicoPen *pen){
    pen->clear();
    pen->printAtF(0, 0, false,"%-*s[ %-*s]", _wizCol1Width, "TIMS PLUS drive B",
      _wizCol2Width, _timsDiskNameB.empty() ? "-" : _timsDiskNameB.c_str());
  });

  _timsDiskAOp.toggle([=]() {
    _chooseTimsDiskA.reload();
    _wiz.push(
      &_chooseTimsDiskA,
      [=](PicoPen *pen){ pen->printAtF(0, 0, false, "%s", "TIMS PLUS disk images (A:)"); },
      true);
  });
  _timsDiskBOp.toggle([=]() {
    _chooseTimsDiskB.reload();
    _wiz.push(
      &_chooseTimsDiskB,
      [=](PicoPen *pen){ pen->printAtF(0, 0, false, "%s", "TIMS PLUS disk images (B:)"); },
      true);
  });

  _chooseTimsDiskA.onToggle = [=](FILINFO *finfo, int32_t i, const char* path) {
    if (_timsMountA >= 0) {
      _zxSpectrum->hcFdc().eject(_timsMountA);
      if (_timsA) delete _timsA;
      _timsDiskNameA.clear();
      _timsMountA = -1;
    }
    _timsA = new FatFsSpiInputStream(_sdCard, path);
    if (_zxSpectrum->hcFdc().mount(0, _timsA)) {
      _timsDiskNameA = finfo->fname;
      _timsMountA = 0;
    } else {
      delete _timsA; _timsA = 0;
      _wizUtils.showMessage([=](PicoPen *pen){ pen->clear(); pen->printAt(0,1,false,"Unsupported TIMS PLUS DSK"); });
    }
    _wiz.pop(true);
  };

  _chooseTimsDiskB.onToggle = [=](FILINFO *finfo, int32_t i, const char* path) {
    if (_timsMountB >= 0) {
      _zxSpectrum->hcFdc().eject(_timsMountB);
      if (_timsB) delete _timsB;
      _timsDiskNameB.clear();
      _timsMountB = -1;
    }
    _timsB = new FatFsSpiInputStream(_sdCard, path);
    if (_zxSpectrum->hcFdc().mount(1, _timsB)) {
      _timsDiskNameB = finfo->fname;
      _timsMountB = 1;
    } else {
      delete _timsB; _timsB = 0;
      _wizUtils.showMessage([=](PicoPen *pen){ pen->clear(); pen->printAt(0,1,false,"Unsupported TIMS PLUS DSK"); });
    }
    _wiz.pop(true);
  };

  _chooseTimsDiskA.onRefreshDisplay = [=]() { _hcDisk.repaint(); };
  _chooseTimsDiskB.onRefreshDisplay = [=]() { _hcDisk.repaint(); };
#endif

#ifdef ZX_PLUS3_ROM_AVAILABLE
  _resetPlus3Op.toggle([=]() {
    _snapName.clear();
    _zxSpectrum->reset(ZxSpectrumPlus3);
    _wiz.pop(true);
  });
#endif

  // CoBra 1
  _cobraOp.onPaint([=](PicoPen *pen){
    pen->clear();
    pen->printAtF(0, 0, false,"%-*s", _wizCol1Width, "CoBra 1");
  });
  _cobraOp.toggle([=]() {
    _wiz.push(
      &_cobra,
      [](PicoPen *pen){ pen->printAt(0, 0, false, "CoBra 1 - floppy v1.2, mainboard v0.2"); },
      true);
  });

#if PICO_RP2350
  // Boot Manager: flash /sys/bootman_rp2350.bin into slot 0 and reboot.
  // Two-step confirm (same pattern as System > Boot select); success never
  // returns (watchdog reboot into the boot manager browser). No quick key.
  _bootManOp.onPaint([=](PicoPen *pen){
    pen->clear();
    pen->printAtF(0, 0, false, "%-*s", _wizCol1Width, "-> Boot Manager");
  });
  _bootManOp.toggle([=]() {
    _wizUtils.confirm(
      [=](PicoPen *pen){
        pen->printAtF(2, 0, false, "Flash boot manager? (%s)", BOOT_SWAP_PATH_BOOTMAN);
      },
      [=]() {
        if (!bootSwapFromSd(BOOT_SWAP_PATH_BOOTMAN)) {
          // Refused by a gate (missing file, PICOBIN/RP2040 image, ...):
          // boot swap declined, flash not touched - inform the user.
          _wizUtils.showMessage([=](PicoPen *pen){
            pen->clear();
            pen->printAt(0, 1, false, "Boot manager image refused");
            pen->printAt(0, 2, false, "Check Uart log / /sys file");
          });
        }
      }
    );
  });
#endif
  for (uint8_t d = 0; d < 4; ++d) {
    _cobra.addOption(_cobraDiskOp[d].addQuickKey(getQuickKey(d)));
    _cobraDiskOp[d].onPaint([=](PicoPen *pen){
      pen->clear();
      char label[8];
      snprintf(label, sizeof(label), "Disk %c:", 'A' + d);
      pen->printAtF(0, 0, false,"%-*s[ %-*s]", _wizCol1Width, label, _wizCol2Width, _cobraDiskName[d].c_str());
    });
    _cobraDiskOp[d].toggle([=]() {
      _cobraDrive = d;
      _wiz.push(
        &_chooseCobraDisk,
        [=](PicoPen *pen){ pen->printAtF(0, 0, false, "Disk %c: (IMG 720K/SYS, DSK, EDSK)", 'A' + d); },
        true);
    });
  }
  _cobra.addOption(_cobraEjectOp.addQuickKey(&_k5));
  _cobra.addOption(_cobraConfigOp.addQuickKey(&_k6));
  _cobra.addOption(_cobraInterleaveOp.addQuickKey(&_k7));
  _cobra.addOption(_cobraResetBasicOp.addQuickKey(&_k8));
  _cobra.enableQuickKeys();
  _cobraEjectOp.toggle([=]() {
    for (uint8_t d = 0; d < 4; ++d) cobraEject(d);
    _cobra.repaint();
  });
  _cobraConfigOp.onPaint([=](PicoPen *pen){
    pen->clear();
    const char *m;
    switch(_zxSpectrum->cobraConfig()) {
      case Cobra80K80Col: m = "80K + 80 chr/lin"; break;
      case Cobra80K: m = "80K (Modif. 80K)"; break;
      default: m = "64K (Std. / Modif. 64K)"; break;
    }
    pen->printAtF(0, 0, false,"%-*s[ %-*s]", _wizCol1Width, "Jumpers", _wizCol2Width, m);
  });
  _cobraConfigOp.toggle([=]() {
    // Re-jumpering the board: takes effect with a power-on reset.
    const CobraConfig c = _zxSpectrum->cobraConfig();
    _zxSpectrum->cobraConfig(c == Cobra80K80Col ? Cobra80K : c == Cobra80K ? Cobra64K : Cobra80K80Col);
    if (_zxSpectrum->type() == ZxSpectrumCobra) _zxSpectrum->reset(ZxSpectrumCobra);
    _cobra.repaint();
  });
  _cobraInterleaveOp.onPaint([=](PicoPen *pen){
    pen->clear();
    pen->printAtF(0, 0, false,"%-*s[ %-*s]", _wizCol1Width, "IMG 18x256", _wizCol2Width,
      CobraDisk::rawInterleaved() ? "interleaved (1,10,2,11..)" : "sector order (1,2,3..)");
  });
  _cobraInterleaveOp.toggle([=]() {
    CobraDisk::rawInterleaved(!CobraDisk::rawInterleaved());
    _cobra.repaint();
  });
  _cobraResetBasicOp.toggle([=]() {
    _zxSpectrum->cobraResetBasic();
    _wiz.pop(true);
  });
  _chooseCobraDisk.onToggle = [=](FILINFO *finfo, int32_t i, const char* path) {
    const uint8_t d = _cobraDrive;
    cobraEject(d);
    CobraDiskFile *f = new CobraDiskFile(_sdCard, path);
    if (!f->isOpen() || !_zxSpectrum->cobraInsertDisk(d, f)) {
      delete f;
      _cobraDiskName[d] = "? ";
      _cobraDiskName[d] += finfo->fname;
    }
    else {
      _cobraDiskFile[d] = f;
      _cobraDiskName[d] = finfo->fname;
      if (!f->writable()) _cobraDiskName[d] += " (R/O)";
    }
    _wiz.pop(true);
  };

  // ICE Felix HC2000 disk drive mount / eject (zxspectrum/disks/hc2000)
  _main.addOption(_hcDiskOp.addQuickKey(&_kD));

  // Custom 48K ZX Spectrum ROM (main menu option R, right after the D disk
  // option): load a 16384-byte .rom image from zxspectrum/roms into the
  // ZxSpectrum custom-ROM RAM buffer. The 48K machine then boots it instead
  // of the built-in flash ROM; the row label shows the loaded file name.
  _main.addOption(_zx48kRomOp.addQuickKey(&_kR));
  _zx48kRomOp.onPaint([=](PicoPen *pen){
    pen->clear();
    pen->printAtF(0, 0, false, "%-*s[ %-*s]", _wizCol1Width, "ZX48K Custom ROM",
      _wizCol2Width, _zx48kRomName.empty() ? "-" : _zx48kRomName.c_str());
  });
  _zx48kRomOp.toggle([=]() {
    _chooseZx48kRom.reload();
    _wiz.push(
      &_chooseZx48kRom,
      [=](PicoPen *pen){ pen->printAt(0, 0, false, "16K ROM images (48K)"); },
      true);
  });
  _chooseZx48kRom.onToggle = [=](FILINFO *finfo, int32_t i, const char* path) {
    if (isCustom48kRom(finfo->fsize, finfo->fname)) {
      uint8_t *rom = _zxSpectrum->customRom(); // heap-allocates on first use
      if (rom) {
        FatFsSpiInputStream *is = new FatFsSpiInputStream(_sdCard, path);
        const int32_t n = is->read(rom, 16384);
        delete is;
        if (n == 16384) {
          _zx48kRomName = finfo->fname;
          // Already running the 48K machine: reset immediately so the custom
          // ROM boots. Any other active machine just keeps the buffer stored
          // (it takes effect when the user switches to 48K).
          if (_zxSpectrum->type() == ZxSpectrum48k) _zxSpectrum->reset(ZxSpectrum48k);
          _main.repaint();
        }
        else {
          // Short read: drop the half-loaded image again
          _zxSpectrum->customRomLoaded(false);
          _wizUtils.showMessage([=](PicoPen *pen){ pen->clear(); pen->printAt(0,1,false,"Unsupported ROM"); });
        }
      }
      else {
        _wizUtils.showMessage([=](PicoPen *pen){ pen->clear(); pen->printAt(0,1,false,"Unsupported ROM"); });
      }
    }
    else {
      _wizUtils.showMessage([=](PicoPen *pen){ pen->clear(); pen->printAt(0,1,false,"Unsupported ROM"); });
    }
    _wiz.pop(true);
  };
  _chooseZx48kRom.onRefreshDisplay = [=]() { _main.repaint(); };
  _hcDiskOp.toggle([=]() {
    _wiz.push(
      &_hcDisk,
      [=](PicoPen *pen){
        pen->printAt(0, 0, false, "SE APASA TASTA 5 PENTRU REFRESH SD CARD");
        pen->printAtF(0, 1, false, "Disk images  [ %s%s]",
        _hcDiskNameA.empty() ? "-" : _hcDiskNameA.c_str(),
        _hcDiskNameB.empty() ? "" : _hcDiskNameB.c_str()); },
      true);
  });

  _hcDisk.addOption(_hcDiskAOp.addQuickKey(&_k1));
  _hcDisk.addOption(_hcDiskBOp.addQuickKey(&_k2));
  _hcDisk.addOption(_hcDiskEjectOp.addQuickKey(&_k3));
  _hcDisk.addOption(_hcBasicDiskAOp.addQuickKey(&_k5));
#ifdef ZX_PLUS3_ROM_AVAILABLE
  _hcDisk.addOption(_plus3DiskAOp.addQuickKey(&_k6));
  _hcDisk.addOption(_plus3DiskBOp.addQuickKey(&_k7));
#endif
#if PICO_RP2350
  // TIM S Plus disk mounts (phase 2, RP2350 only): last rows of the disk
  // menu ("D. HC2K..." -> Disk images), quick keys k8/k9.
  // k8 = drive A (FDC unit 0), k9 = drive B (FDC unit 1).
  _hcDisk.addOption(_timsDiskAOp.addQuickKey(&_k8));
  _hcDisk.addOption(_timsDiskBOp.addQuickKey(&_k9));
#endif
  // ICE Felix CUB-Z drives (rows 10 and 11, quick keys A/B - digits 1-9
  // are taken by the rows above)
  _hcDisk.addOption(_cubzDiskAOp.addQuickKey(&_kA));
  _hcDisk.addOption(_cubzDiskBOp.addQuickKey(&_kB));
  for (uint8_t d = 0; d < 2; ++d) {
    PicoOptionText *op = d ? &_cubzDiskBOp : &_cubzDiskAOp;
    op->onPaint([=](PicoPen *pen){
      pen->clear();
      pen->printAtF(0, 0, false,"%-*s[ %-*s]", _wizCol1Width, d ? "CUB-Z drive B" : "CUB-Z drive A",
        _wizCol2Width, _cubzDiskName[d].empty() ? "-" : _cubzDiskName[d].c_str());
    });
    PicoWizExplorer *ex = d ? &_chooseCubzDiskB : &_chooseCubzDiskA;
    op->toggle([=]() {
      ex->reload();
      _wiz.push(
        ex,
        [=](PicoPen *pen){ pen->printAtF(0, 0, false, "CUB-Z disk images (%c:)  5=REFRESH", 'A' + d); },
        true);
    });
    ex->onToggle = [=](FILINFO *finfo, int32_t i, const char* path) {
      cubzMount(d, finfo, path);
      _wiz.pop(true);
    };
  }
  // ICE Felix HC88 disk rows 12/13 (quick keys C/D, reusing
  // the shared _kC/_kD matchers): the same chooser + mount machinery as the
  // TIMS PLUS rows, pointed at the dedicated folder zxspectrum/disks/HC88
  // (auto-created at menu init). Drive A = FDC unit 0, drive B = FDC unit 1
  // (fuse hc88_fdc.c: HC-88 BIOS only addresses units 0/1). The row label
  // shows the mounted file name, TIMS PLUS style.
  _hcDisk.addOption(_hc88DiskAOp.addQuickKey(&_kC));
  _hcDisk.addOption(_hc88DiskBOp.addQuickKey(&_kD));
  _hc88DiskAOp.onPaint([=](PicoPen *pen){
    pen->clear();
    pen->printAtF(0, 0, false,"%-*s[ %-*s]", _wizCol1Width, "HC88 drive A",
      _wizCol2Width, _hc88DiskNameA.empty() ? "-" : _hc88DiskNameA.c_str());
  });
  _hc88DiskBOp.onPaint([=](PicoPen *pen){
    pen->clear();
    pen->printAtF(0, 0, false,"%-*s[ %-*s]", _wizCol1Width, "HC88 drive B",
      _wizCol2Width, _hc88DiskNameB.empty() ? "-" : _hc88DiskNameB.c_str());
  });

  _hc88DiskAOp.toggle([=]() {
    _chooseHc88DiskA.reload();
    _wiz.push(
      &_chooseHc88DiskA,
      [=](PicoPen *pen){ pen->printAtF(0, 0, false, "%s", "HC88 disk images (A:)"); },
      true);
  });
  _hc88DiskBOp.toggle([=]() {
    _chooseHc88DiskB.reload();
    _wiz.push(
      &_chooseHc88DiskB,
      [=](PicoPen *pen){ pen->printAtF(0, 0, false, "%s", "HC88 disk images (B:)"); },
      true);
  });

  _chooseHc88DiskA.onToggle = [=](FILINFO *finfo, int32_t i, const char* path) {
    if (_hc88MountA >= 0) {
      _zxSpectrum->hcFdc().eject(_hc88MountA);
      if (_hc88A) delete _hc88A;
      _hc88DiskNameA.clear();
      _hc88MountA = -1;
    }
    _hc88A = new FatFsSpiInputStream(_sdCard, path);
    if (_zxSpectrum->hcFdc().mount(0, _hc88A)) {
      _hc88DiskNameA = finfo->fname;
      _hc88MountA = 0;
    } else {
      delete _hc88A; _hc88A = 0;
      _wizUtils.showMessage([=](PicoPen *pen){ pen->clear(); pen->printAt(0,1,false,"Unsupported HC88 DSK"); });
    }
    _wiz.pop(true);
  };

  _chooseHc88DiskB.onToggle = [=](FILINFO *finfo, int32_t i, const char* path) {
    if (_hc88MountB >= 0) {
      _zxSpectrum->hcFdc().eject(_hc88MountB);
      if (_hc88B) delete _hc88B;
      _hc88DiskNameB.clear();
      _hc88MountB = -1;
    }
    _hc88B = new FatFsSpiInputStream(_sdCard, path);
    if (_zxSpectrum->hcFdc().mount(1, _hc88B)) {
      _hc88DiskNameB = finfo->fname;
      _hc88MountB = 1;
    } else {
      delete _hc88B; _hc88B = 0;
      _wizUtils.showMessage([=](PicoPen *pen){ pen->clear(); pen->printAt(0,1,false,"Unsupported HC88 DSK"); });
    }
    _wiz.pop(true);
  };

  _chooseHc88DiskA.onRefreshDisplay = [=]() { _hcDisk.repaint(); };
  _chooseHc88DiskB.onRefreshDisplay = [=]() { _hcDisk.repaint(); };
  _hcDisk.enableQuickKeys();

  _hcDiskAOp.toggle([=]() {
    _chooseHcDiskA.reload();
    _wiz.push(
      &_chooseHcDiskA,
      [=](PicoPen *pen){ pen->printAtF(0, 0, false, "%s", "HC2000 disk images (A:)"); },
      true);
  });

  _hcBasicDiskAOp.toggle([=]() {
    _chooseBasicDiskA.reload();
    _wiz.push(
      &_chooseBasicDiskA,
      [=](PicoPen *pen){ pen->printAt(0, 0, false, "BASIC disk images (A:)"); },
      true);
  });

  _hcDiskBOp.toggle([=]() {
    _chooseHcDiskB.reload();
    _wiz.push(
      &_chooseHcDiskB,
      [=](PicoPen *pen){ pen->printAtF(0, 0, false, "%s", "HC2000 disk images (B:)"); },
      true);
  });

#ifdef ZX_PLUS3_ROM_AVAILABLE
  _plus3DiskAOp.toggle([=]() {
    _choosePlus3DiskA.reload();
    _wiz.push(
      &_choosePlus3DiskA,
      [=](PicoPen *pen){ pen->printAt(0, 0, false, "+3 disk images (A:)"); },
      true);
  });
  _plus3DiskBOp.toggle([=]() {
    _choosePlus3DiskB.reload();
    _wiz.push(
      &_choosePlus3DiskB,
      [=](PicoPen *pen){ pen->printAt(0, 0, false, "+3 disk images (B:)"); },
      true);
  });
#endif

  _hcDiskEjectOp.toggle([=]() {
    if (_hcMountA >= 0) { _zxSpectrum->hcFdc().eject(_hcMountA); if (_hdisA) { delete _hdisA; _hdisA = 0; } _hcDiskNameA.clear(); _hcMountA = -1; }
    if (_hcMountB >= 0) { _zxSpectrum->hcFdc().eject(_hcMountB); if (_hdisB) { delete _hdisB; _hdisB = 0; } _hcDiskNameB.clear(); _hcMountB = -1; }
    _wiz.pop(true);
  });

  _chooseHcDiskA.onToggle = [=](FILINFO *finfo, int32_t i, const char* path) {
    if (_hcMountA >= 0) { _zxSpectrum->hcFdc().eject(_hcMountA); if (_hdisA) { delete _hdisA; } }
    _hdisA = new FatFsSpiInputStream(_sdCard, path);
    if (_zxSpectrum->hcFdc().mount(0, _hdisA)) {
      _hcDiskNameA = finfo->fname;
      _hcMountA = 0;
    } else {
      delete _hdisA; _hdisA = 0;
      _wizUtils.showMessage([=](PicoPen *pen){ pen->clear(); pen->printAt(0,1,false,"Not an HC2000 DSK image"); });
    }
    _wiz.pop(true);
  };

  _chooseBasicDiskA.onToggle = [=](FILINFO *finfo, int32_t i, const char* path) {
    if (_hcMountA >= 0) {
      _zxSpectrum->hcFdc().eject(_hcMountA);
      if (_hdisA) delete _hdisA;
    }
    _hdisA = new FatFsSpiInputStream(_sdCard, path);
    if (_zxSpectrum->hcFdc().mount(0, _hdisA)) {
      _hcDiskNameA = finfo->fname;
      _hcMountA = 0;
    } else {
      delete _hdisA; _hdisA = 0;
      _wizUtils.showMessage([=](PicoPen *pen){ pen->clear(); pen->printAt(0,1,false,"Unsupported BASIC disk"); });
    }
    _wiz.pop(true);
  };

#ifdef ZX_PLUS3_ROM_AVAILABLE
  _choosePlus3DiskA.onToggle = [=](FILINFO *finfo, int32_t i, const char* path) {
    if (_hcMountA >= 0) {
      _zxSpectrum->hcFdc().eject(_hcMountA);
      if (_hdisA) delete _hdisA;
      _hcDiskNameA.clear();
      _hcMountA = -1;
    }
    _hdisA = new FatFsSpiInputStream(_sdCard, path);
    if (_zxSpectrum->hcFdc().mount(0, _hdisA, true)) {
      _hcDiskNameA = finfo->fname;
      _hcMountA = 0;
    } else {
      delete _hdisA; _hdisA = 0;
      _wizUtils.showMessage([=](PicoPen *pen){ pen->clear(); pen->printAt(0,1,false,"Unsupported +3 DSK"); });
    }
    _wiz.pop(true);
  };
  _choosePlus3DiskB.onToggle = [=](FILINFO *finfo, int32_t i, const char* path) {
    if (_hcMountB >= 0) {
      _zxSpectrum->hcFdc().eject(_hcMountB);
      if (_hdisB) delete _hdisB;
      _hcDiskNameB.clear();
      _hcMountB = -1;
    }
    _hdisB = new FatFsSpiInputStream(_sdCard, path);
    if (_zxSpectrum->hcFdc().mount(1, _hdisB, true)) {
      _hcDiskNameB = finfo->fname;
      _hcMountB = 1;
    } else {
      delete _hdisB; _hdisB = 0;
      _wizUtils.showMessage([=](PicoPen *pen){ pen->clear(); pen->printAt(0,1,false,"Unsupported +3 DSK"); });
    }
    _wiz.pop(true);
  };
#endif

  _chooseHcDiskB.onToggle = [=](FILINFO *finfo, int32_t i, const char* path) {
    if (_hcMountB >= 0) { _zxSpectrum->hcFdc().eject(_hcMountB); if (_hdisB) { delete _hdisB; } }
    _hdisB = new FatFsSpiInputStream(_sdCard, path);
    if (_zxSpectrum->hcFdc().mount(1, _hdisB)) {
      _hcDiskNameB = finfo->fname;
      _hcMountB = 1;
    } else {
      delete _hdisB; _hdisB = 0;
      _wizUtils.showMessage([=](PicoPen *pen){ pen->clear(); pen->printAt(0,1,false,"Not an HC2000 DSK image"); });
    }
    _wiz.pop(true);
  };

  _chooseHcDiskA.onRefreshDisplay = [=]() { _hcDisk.repaint(); };
  _chooseBasicDiskA.onRefreshDisplay = [=]() { _hcDisk.repaint(); };
#ifdef ZX_PLUS3_ROM_AVAILABLE
  _choosePlus3DiskA.onRefreshDisplay = [=]() { _hcDisk.repaint(); };
  _choosePlus3DiskB.onRefreshDisplay = [=]() { _hcDisk.repaint(); };
#endif
  _chooseHcDiskB.onRefreshDisplay = [=]() { _hcDisk.repaint(); };

  _reset48kOp.toggle([=]() {
    _snapName.clear();
    _zxSpectrum->reset(ZxSpectrum48k);
    _wiz.pop(true);
  });
  _reset128kOp.toggle([=]() {
    _snapName.clear();
    _zxSpectrum->reset(ZxSpectrum128k);
    _wiz.pop(true);
  });

  _joystick.addOption(_joystickKempstonOp.addQuickKey(&_k1));
  _joystick.addOption(_joystickSinclairLROp.addQuickKey(&_k2));
  _joystick.addOption(_joystickSinclairRLOp.addQuickKey(&_k3));
  _joystick.enableQuickKeys();

  _joystickKempstonOp.toggle([=]() {
    if (_zxSpectrum->joystick()) {
      _zxSpectrum->joystick()->mode(ZxSpectrumJoystickModeKempston);
    }
    _wiz.pop(true);
  });
  _joystickSinclairLROp.toggle([=]() {
    if (_zxSpectrum->joystick()) {
      _zxSpectrum->joystick()->mode(ZxSpectrumJoystickModeSinclairLR);
    }
    _wiz.pop(true);
  });
  _joystickSinclairRLOp.toggle([=]() {
    if (_zxSpectrum->joystick()) {
      _zxSpectrum->joystick()->mode(ZxSpectrumJoystickModeSinclairRL);
    }
    _wiz.pop(true);
  });

  _mouse.addOption(_mouseKempstonMouseOp.addQuickKey(&_k1));
  _mouse.addOption(_mouseKempstonJoystickOp.addQuickKey(&_k2));
  _mouse.addOption(_mouseSinclairJoystickLOp.addQuickKey(&_k3));
  _mouse.addOption(_mouseSinclairJoystickROp.addQuickKey(&_k4));
  _mouse.enableQuickKeys();

  _mouseKempstonMouseOp.toggle([=]() {
    if (_zxSpectrum->mouse()) {
      _zxSpectrum->mouse()->mouseMode(ZxSpectrumMouseModeKempstonMouse);
    }
    _wiz.pop(true);
  });
  _mouseKempstonJoystickOp.toggle([=]() {
    if (_zxSpectrum->mouse()) {
      _zxSpectrum->mouse()->mouseMode(ZxSpectrumMouseModeJoystick);
      _zxSpectrum->mouse()->mode(ZxSpectrumJoystickModeKempston);
    }
    _wiz.pop(true);
  });
  _mouseSinclairJoystickLOp.toggle([=]() {
    if (_zxSpectrum->mouse()) {
      _zxSpectrum->mouse()->mouseMode(ZxSpectrumMouseModeJoystick);
      _zxSpectrum->mouse()->mode(ZxSpectrumJoystickModeSinclairLR);
    }
    _wiz.pop(true);
  });
  _mouseSinclairJoystickROp.toggle([=]() {
    if (_zxSpectrum->mouse()) {
      _zxSpectrum->mouse()->mouseMode(ZxSpectrumMouseModeJoystick);
      _zxSpectrum->mouse()->mode(ZxSpectrumJoystickModeSinclairRL);
    }
    _wiz.pop(true);
  });
  uint audioOutQuickKeyIndex = 0;
#ifdef PICO_PIO_PWM_AUDIO  
_audioOutOptions.addOption(_audioPioPwmOp.addQuickKey(getQuickKey(audioOutQuickKeyIndex++)));
  _audioPioPwmOp.toggle([=]() {
    _zxSpectrum->setAudioDriver(&_zx_spectrum_audio_drivers[zx_spectrum_audio_driver_pio_pwm_index]);
    _wiz.pop(true);
  });
#endif
#ifdef PICO_PWM_AUDIO  
_audioOutOptions.addOption(_audioPwmOp.addQuickKey(getQuickKey(audioOutQuickKeyIndex++)));
  _audioPwmOp.toggle([=]() {
    _zxSpectrum->setAudioDriver(&_zx_spectrum_audio_drivers[zx_spectrum_audio_driver_pwm_index]);
    _wiz.pop(true);
  });
#endif
#ifdef PICO_AUDIO_I2S  
_audioOutOptions.addOption(_audioI2sOp.addQuickKey(getQuickKey(audioOutQuickKeyIndex++)));
  _audioI2sOp.toggle([=]() {
    _zxSpectrum->setAudioDriver(&_zx_spectrum_audio_drivers[zx_spectrum_audio_driver_i2s_index]);
    _wiz.pop(true);
  });
#endif
#ifdef PICO_HDMI_AUDIO  
_audioOutOptions.addOption(_audioHdmiOp.addQuickKey(getQuickKey(audioOutQuickKeyIndex++)));
  _audioHdmiOp.toggle([=]() {
    _zxSpectrum->setAudioDriver(&_zx_spectrum_audio_drivers[zx_spectrum_audio_driver_hdmi_index]);
    _wiz.pop(true);
  });
#endif
  _audioOutOptions.enableQuickKeys();

  _videoOp.toggle([=]() {
    const char *k = videoDriverName();
    const char *m = getZxSpectrumVideoDriver((zx_spectrum_video_driver_enum_t)_videoDriverIndex)->name;
    _wiz.push(
      &_videoOptions, 
      [=](PicoPen *pen){ 
        pen->printAtF(0, 0, false, "Video output [%s,%s]", k,m); 
      }, 
      true);
  });
  uint videoQuickKeyIndex = 0;
#if defined(CVBS_12MHZ) || defined(CVBS_13_5MHZ) || defined(VGA_MODE)
  _videoOptions.addOption(_videoVgaOp.addQuickKey(getQuickKey(videoQuickKeyIndex++)));
  _videoVgaOp.toggle([=]() {
    _videoDriverIndex = (uint8_t)zx_spectrum_video_driver_vga_index;
    _wiz.pop(true);
  });
#endif
#if defined(DVI_DEFAULT_SERIAL_CONFIG)
  _videoOptions.addOption(_videoDviOp.addQuickKey(getQuickKey(videoQuickKeyIndex++)));
  _videoDviOp.toggle([=]() {
    _videoDriverIndex = (uint8_t)zx_spectrum_video_driver_dvi_index;
    _wiz.pop(true);
  });
#endif
#if defined(PICO_ST7789_LCD)
  _videoOptions.addOption(_videoLcdOp.addQuickKey(getQuickKey(videoQuickKeyIndex++)));
  _videoLcdOp.toggle([=]() {
    _videoDriverIndex = (uint8_t)zx_spectrum_video_driver_lcd_index;
    _wiz.pop(true);
  });
#endif
  _videoOptions.enableQuickKeys();

  _tapePlayer.addOption(_chooseTapeOp.addQuickKey(&_k1));
  _tapePlayer.addOption(_ejectTapeOp.addQuickKey(&_k2));
  _tapePlayer.addOption(_pauseTapeOp.addQuickKey(&_k3));
  
  _tapePlayer.enableQuickKeys();

  _chooseTapeOp.toggle([=]() {
    _wiz.push(
      &_chooseTape, 
      [](PicoPen *pen){ 
        pen->printAtF(0, 0, false,"%s  [%s]", "Tapes", 
          SZ_FRAME_COLS < 50 ? "1=DE 2=RN 3=CP 4=PA 5=RF" : "1=DEL 2=REN 3=CPY 4=PST 5=REF"
        );
      },
      true);
  });

  _ejectTapeOp.toggle([=]() {
    ejectTape();
    _wiz.repaint();
  });
  
  _pauseTapeOp.onPaint([=](PicoPen *pen){
    pen->clear();
    pen->printAtF(0, 0, false,"%-*s[ %-*s]", _wizCol1Width, "Motor", _wizCol2Width, _zxSpectrum->tapePaused() ? "off" : "on");
  });
  _pauseTapeOp.toggle([=]() {
    _zxSpectrum->togglePauseTape();
  });
  
  _chooseTape.onToggle = [=](FILINFO *finfo, int32_t i, const char* path) {
    ejectTape();
    _tapeName = finfo->fname;
    _tis = new FatFsSpiInputStream(_sdCard, path);
    const char *ext = fext(finfo->fname);
    if (0 == strcmp(ext, "tzx") || 0 == strcmp(ext, "TZX")) {
      _zxSpectrum->loadTzx(_tis);
    }
    else {
      _zxSpectrum->loadTap(_tis); 
    }
    _wiz.pop(true);
  };

  onPaint([](PicoPen *pen) {
#ifdef ZX_PLATFORM
     pen->printAtF(0, 0, false, "ZX Spectrum 48K/128K by fruit-bat on %s", ZX_PLATFORM);
#else
     pen->printAt(0, 0, false, "ZX Spectrum 48K/128K by fruit-bat");
#endif
     pen->printAtF(0, 1, false, "Built for %s on %s",
        PICO_MCU,
        __DATE__
      );

     pen->printAt(0, SZ_FRAME_ROWS-1, false, "F1 to exit menu");
     pen->printAt(SZ_FRAME_COLS-14, SZ_FRAME_ROWS-1, false, "ESC to go back");
#if SZ_FRAME_COLS >= 52
     // Author credit, lowercase, centered between the two hints above
     pen->printAt((SZ_FRAME_COLS-22)/2, SZ_FRAME_ROWS-1, false, "by bizonul66@yahoo.com");
#endif
   }); 
}

void ZxSpectrumMenu::snapName(const char* name) {
  _snapName = name;
}

void ZxSpectrumMenu::ejectTape() {
  _zxSpectrum->ejectTape();
  _tapeName.clear();
  if (_tis) {
    delete _tis;
    _tis = 0;
  }
}

void ZxSpectrumMenu::showMessage(std::function<void(PicoPen *pen)> message) {
  _wizUtils.showMessage(message);
}

void ZxSpectrumMenu::refresh(std::function<void()> refresh) {
  _chooseSnap.onRefreshDisplay = refresh;
  _chooseTape.onRefreshDisplay = refresh;
}

void ZxSpectrumMenu::removeMessage() {
  _wizUtils.removeMessage();
}

void ZxSpectrumMenu::showTzxOptions() {
  _wiz.push(
    &_tzxSelect, 
    [](PicoPen *pen){ pen->printAt(0, 0, false, "Choose TZX tape option"); },
    true);
}

void ZxSpectrumMenu::clearTzxOptions() {
  _tzxSelect.deleteOptions();
}

void ZxSpectrumMenu::addTzxOption(const char *s) {
  _tzxSelect.addOption(new PicoOptionText(s));
}

bool ZxSpectrumMenu::isZ80(const char* filename) {
    const char *ext = fext(filename);
    return (0 == strcmp(ext, "z80") || 0 == strcmp(ext, "Z80"));
}

void ZxSpectrumMenu::nextSnap(int d) {
  _chooseSnap.next([](FILINFO* finfo) {
    return !(finfo->fattrib & AM_DIR) && isZ80(finfo->fname);
  }, d);
}

void ZxSpectrumMenu::quickSave(int slot) {

  // Make sure we have a folder for the quick-save
  if (!_pathQuickSaves.createFolders(_sdCard)) return;
  
  {
    char name[32];
    snprintf(name, 32, "Slot %d.z80", slot + 1);

    DBG_PRINTF("ZxSpectrumMenu: Quick save file to save name '%s'\n", name);
    
    FatFsFilePath pname(&_pathQuickSaves, name);
    std::string fullpath;
    pname.appendTo(fullpath);
    
    DBG_PRINTF("ZxSpectrumMenu: Quick save file to save path '%s'\n", fullpath.c_str());
    
    FatFsSpiOutputStream os(_sdCard, fullpath.c_str());
    
    _zxSpectrum->saveZ80(&os);
  }
  
  {
    std::string fullpath;
    _pathQuickSaves.appendTo(fullpath);
    _chooseSnap.folderModified(fullpath.c_str());
  }
}

void ZxSpectrumMenu::quickLoad(int slot) {
  char name[32];
  snprintf(name, 32, "Slot %d.z80", slot + 1);

  DBG_PRINTF("ZxSpectrumMenu: Quick save file to load name '%s'\n", name);
  
  FatFsFilePath pname(&_pathQuickSaves, name);
  std::string fullpath;
  pname.appendTo(fullpath);
  
  DBG_PRINTF("ZxSpectrumMenu: Quick save file to load path '%s'\n", fullpath.c_str());  
  
  FatFsSpiInputStream is(_sdCard, fullpath.c_str());
  
  if (is.closed()) {
    DBG_PRINTF("ZxSpectrumMenu: No quick save in slot '%d' to load\n", slot);  
    return;
  }
  
  _zxSpectrum->loadZ80(&is);
  
  snapName(name);
}

void ZxSpectrumMenu::initialise() {
 
  loadSettings();

  _pathQuickSaves.createFolders(_sdCard);
  _pathTapes.createFolders(_sdCard);
  // Custom 48K ROM images folder (main menu option R): auto-created like
  // the disks/HC88 folders below.
  _pathRoms.createFolders(_sdCard);
  _pathDisks.createFolders(_sdCard);
  _pathDisksHc2000.createFolders(_sdCard);
  _pathDisksBasic.createFolders(_sdCard);
  _pathDisksCubz.createFolders(_sdCard);
#if PICO_RP2350
  _pathDisksTims.createFolders(_sdCard);
  // ICE Felix HC88: auto-create zxspectrum/disks/HC88 at menu init, same
  // pattern as the TIMS PLUS folder above.
  _pathDisksHc88.createFolders(_sdCard);
#endif
#ifdef ZX_PLUS3_ROM_AVAILABLE
  _pathDisksPlus3.createFolders(_sdCard);
#endif

  _chooseTape.load();
  _chooseSnap.load();
}

void ZxSpectrumMenu::saveSettings() {
  ZxSpectrumSettingValues settings;
  _zxSpectrumSettings->load(&settings);
  settings.volume = zxSpectrumAudioGetVolume();
  settings.joystickMode = _zxSpectrum->joystick()->mode();
  settings.mouseMode = _zxSpectrum->mouse()->mouseMode();
  settings.mouseJoystickMode = _zxSpectrum->mouse()->mode();
  settings.videoDriverDefault = _videoDriverIndex;
  settings.audioDriverDefault[_videoDriverIndex] = 
    zx_spectrum_audio_driver_index(_zxSpectrum->getAudioDriver());

  _zxSpectrumSettings->save(&settings);
  DBG_PRINTF("ZxSpectrumMenu: Saved volume setting '%ld'\n", settings.volume);
  DBG_PRINTF("ZxSpectrumMenu: Saved joystick setting '%d'\n", settings.joystickMode);  
  DBG_PRINTF("ZxSpectrumMenu: Saved mouse setting '%d'\n", settings.mouseMode);  
  DBG_PRINTF("ZxSpectrumMenu: Saved video driver index '%d'\n", settings.videoDriverDefault);  
}

void ZxSpectrumMenu::loadSettings() {
  ZxSpectrumSettingValues settings;
  _zxSpectrumSettings->load(&settings);
  DBG_PRINTF("ZxSpectrumMenu: Loaded volume setting '%ld'\n", settings.volume);
  DBG_PRINTF("ZxSpectrumMenu: Loaded joystick setting '%d'\n", settings.joystickMode);
  DBG_PRINTF("ZxSpectrumMenu: Loaded mouse setting '%d'\n", settings.mouseMode);
  DBG_PRINTF("ZxSpectrumMenu: Loaded video driver index '%d'\n", settings.videoDriverDefault);
  zxSpectrumAudioSetVolume(settings.volume);
  _zxSpectrum->joystick()->mode(settings.joystickMode);
  _zxSpectrum->mouse()->mouseMode(settings.mouseMode);
  _zxSpectrum->mouse()->mode(settings.mouseJoystickMode);
  _videoDriverIndex = settings.videoDriverDefault;
  _zxSpectrum->setAudioDriver(&_zx_spectrum_audio_drivers[settings.audioDriverDefault[_videoDriverIndex]]);
}
