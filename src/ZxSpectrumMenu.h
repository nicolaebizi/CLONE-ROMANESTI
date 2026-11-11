// SPDX-License-Identifier: MIT
// Copyright (c) 2026 fruit-bat
#pragma once

#include "PicoWin.h"
#include "PicoSelect.h"
#include "PicoOptionText.h"
#include "PicoQuickKeyAscii.h"
#include "PicoWiz.h" 
#include "SdCardFatFsSpi.h"
#include "PicoTextField.h"
#include "InputStream.h"
#include <functional>
#include "QuickSave.h"
#include "PicoExplorer.h"
#include "FatFsFilePath.h"

#include "PicoWizUtils.h"
#include "PicoWizExplorer.h"
#include "PicoSlider.h"
#include "PicoOnScreenKeyboard.h"
#include "ZxSpectrumSettings.h"

class ZxSpectrum;
class CobraDiskFile;

class ZxSpectrumMenu : public PicoWin {
private:
  int32_t _wizCol1Width;
  int32_t _wizCol2Width;
  int32_t _wizLeftMargin;
  int32_t _wizCols;
  int32_t _menuRowsPerItem;
  int32_t _explorerRowsPerFile;
  int32_t _explorerRows;
  uint8_t _videoDriverIndex;

  FatFsFilePath _pathZxSpectrum;
  FatFsFilePath _pathSnaps;
  FatFsFilePath _pathTapes;
  FatFsFilePath _pathQuickSaves;
  FatFsFilePath _pathDisks;
  FatFsFilePath _pathCobraDisks;
  FatFsFilePath _pathDisksHc2000;
  FatFsFilePath _pathDisksBasic;
#ifdef ZX_PLUS3_ROM_AVAILABLE
  FatFsFilePath _pathDisksPlus3;
#endif
  SdCardFatFsSpi *_sdCard;
  ZxSpectrum *_zxSpectrum;
  ZxSpectrumSettings *_zxSpectrumSettings;
  InputStream* _tis;

  PicoQuickKeyAscii _k1;
  PicoQuickKeyAscii _k2;
  PicoQuickKeyAscii _k3;
  PicoQuickKeyAscii _k4;
  PicoQuickKeyAscii _k5;
  PicoQuickKeyAscii _k6;
  PicoQuickKeyAscii _k7;
  PicoQuickKeyAscii _k8;
  PicoQuickKeyAscii _k9;
  PicoQuickKeyAscii _kV;
  PicoQuickKeyAscii _kK;
  PicoQuickKeyAscii _kS;
  PicoQuickKeyAscii _kQ;
  PicoQuickKeyAscii _kC;
  PicoQuickKeyAscii _kD;
  // Disk menu (D) rows 10-14: digits 1-9 are taken, so the CUB-Z / HC88
  // rows use letter quick keys A-E (matched case-insensitively).
  PicoQuickKeyAscii _kA;
  PicoQuickKeyAscii _kB;

  PicoWiz _wiz;
  PicoWizUtils _wizUtils;
  PicoSelect _main;

  PicoOption _tapePlayerOp;
  PicoOption _snapOp;
  PicoOption _freqOp;
  PicoOption _intSourceOp;
  PicoOptionText _audioOp;
  PicoOption _resetOp;
#ifdef ZX_PLUS3_ROM_AVAILABLE
  PicoOptionText _mountPlus3BOp;
#endif
  PicoOption _joystickOp;
  PicoOption _mouseOp;
  
  PicoOption _settingsOp;
  PicoOption _keyboardOp;
  PicoOption _systemOp;
  PicoOption _quickSaveOp;
  PicoOption _cobraOp;
#ifdef PICO_RP2350
  // "Boot Manager" flash-from-SD option (BootSwap), RP2350 only: swaps
  // /sys/bootman_rp2350.bin into flash slot 0 and reboots. No quick key.
  PicoOption _bootManOp;
#endif

  PicoSelect _tapePlayer;
  PicoOptionText _chooseTapeOp;
  PicoOptionText _ejectTapeOp;
  PicoOption _pauseTapeOp;

  PicoWizExplorer _chooseTape;
  PicoWizExplorer _chooseSnap;

  // ICE Felix HC2000 disk drive mount / eject (zxspectrum/disks/hc2000)
  PicoSelect _hcDisk;
  PicoOptionText _hcDiskOp;
  PicoOptionText _hcDiskAOp;
  PicoOptionText _hcBasicDiskAOp;
  PicoOptionText _hcDiskBOp;
  PicoOptionText _hcDiskEjectOp;
#ifdef ZX_PLUS3_ROM_AVAILABLE
  PicoOptionText _plus3DiskAOp;
  PicoOptionText _plus3DiskBOp;
#endif
  PicoWizExplorer _chooseHcDiskA;
  PicoWizExplorer _chooseBasicDiskA;
#ifdef ZX_PLUS3_ROM_AVAILABLE
  PicoWizExplorer _choosePlus3DiskA;
  PicoWizExplorer _choosePlus3DiskB;
#endif
  PicoWizExplorer _chooseHcDiskB;
  InputStream* _hdisA;
  InputStream* _hdisB;
  std::string _hcDiskNameA;
  std::string _hcDiskNameB;
  int _hcMountA, _hcMountB;

#ifdef PICO_RP2350
  // TIM S Plus disk drives A/B on the i8272 (fuse tims_fdc: units 0/1,
  // motor from $1FFD bit 3); images live in a dedicated folder of the
  // standard disks dir (zxspectrum/disks/TIMS PLUS). RP2350 only, like the
  // whole TIM machine.
  FatFsFilePath _pathDisksTims;
  PicoOptionText _timsDiskAOp;
  PicoOptionText _timsDiskBOp;
  PicoWizExplorer _chooseTimsDiskA;
  PicoWizExplorer _chooseTimsDiskB;
  InputStream* _timsA;
  InputStream* _timsB;
  std::string _timsDiskNameA;
  std::string _timsDiskNameB;
  int _timsMountA, _timsMountB;

  // ICE Felix HC88 disk chooser rows 12/13 (RP2350 only; quick keys C/D):
  // chooser in zxspectrum/disks/HC88, auto-created at menu init. Phase 2:
  // mounts into hcFdc() drive 0/1 (i8272, exact low-byte ports $DD/$DF/$E7).
  FatFsFilePath _pathDisksHc88;
  PicoOptionText _hc88DiskAOp;
  PicoOptionText _hc88DiskBOp;
  PicoWizExplorer _chooseHc88DiskA;
  PicoWizExplorer _chooseHc88DiskB;
  InputStream* _hc88A;
  InputStream* _hc88B;
  std::string _hc88DiskNameA;
  std::string _hc88DiskNameB;
  int _hc88MountA, _hc88MountB;
#endif

  // ICE Felix CUB-Z disk drives A/B (i8272 units 0/1), images in
  // zxspectrum/disks/CUBZ (IMG, DSK, EDSK; written back to the SD card).
  FatFsFilePath _pathDisksCubz;
  PicoOptionText _cubzDiskAOp;
  PicoOptionText _cubzDiskBOp;
  PicoWizExplorer _chooseCubzDiskA;
  PicoWizExplorer _chooseCubzDiskB;
  CobraDiskFile *_cubzDiskFile[2];
  std::string _cubzDiskName[2];
  void cubzMount(uint8_t drive, FILINFO *finfo, const char *path);

  PicoSelect _audioOptions;
  PicoOption _muteOp;
  PicoOption _audioOutOp;
  PicoOption _volumeOp;

  PicoSelect _reset;
  PicoOptionText _reset48kOp;
  PicoOptionText _reset128kOp;
  PicoOptionText _resetTurboOp;
  PicoOptionText _resetHc2000Op;
  PicoOptionText _resetCobraOp;
#ifdef PICO_RP2350
  // TIM S Plus reset entry (boot-only machine, RP2350 only)
  PicoOptionText _resetTimsOp;
  // ICE Felix HC88 reset entry (phase 1: no FDC, no hires; RP2350 only)
  PicoOptionText _resetHc88Op;
#endif
  PicoOptionText _resetCubzOp;
#ifdef ZX_PLUS3_ROM_AVAILABLE
  PicoOptionText _resetPlus3Op;
#endif

  // CoBra 1: disk drives A:-D: on the floppy interface, mainboard jumpers
  PicoSelect _cobra;
  PicoOption _cobraDiskOp[4];
  PicoOptionText _cobraEjectOp;
  PicoOption _cobraConfigOp;
  PicoOption _cobraInterleaveOp;
  PicoOptionText _cobraResetBasicOp;
  PicoWizExplorer _chooseCobraDisk;
  uint8_t _cobraDrive;
  std::string _cobraDiskName[4];
  CobraDiskFile *_cobraDiskFile[4];
  void cobraEject(uint8_t drive);

  PicoSelect _joystick;
  PicoOptionText _joystickKempstonOp;
  PicoOptionText _joystickSinclairLROp;
  PicoOptionText _joystickSinclairRLOp;

  PicoSelect _mouse;
  PicoOptionText _mouseKempstonMouseOp;
  PicoOptionText _mouseKempstonJoystickOp;
  PicoOptionText _mouseSinclairJoystickLOp;
  PicoOptionText _mouseSinclairJoystickROp;

  PicoSelect _audioOutOptions;
  PicoOptionText _audioNullOp;
  PicoOptionText _audioPioPwmOp;
  PicoOptionText _audioPwmOp;
  PicoOptionText _audioI2sOp;
  PicoOptionText _audioHdmiOp;

  PicoOptionText _videoOp;
  PicoSelect _videoOptions;
  PicoOptionText _videoVgaOp;
  PicoOptionText _videoDviOp;
  PicoOptionText _videoLcdOp;

  PicoSelect _settings;
  PicoOptionText _settingsSaveOp;
  PicoOptionText _settingsLoadOp;

  PicoSelect _system;
  PicoOptionText _systemBootSelOp;

  PicoSlider _volume;
  PicoOnScreenKeyboard _keyboard;

  std::string _tapeName;
  std::string _snapName;

  PicoWin _devices;
  PicoSelect _tzxSelect;
  std::function<void(uint32_t option)> _tzxOption;
  std::function<void(const char *name)> _snapLoadedListener;
   
  void ejectTape();
  
  static bool isZ80(const char* filename);
  
  PicoQuickKeyAscii *getQuickKey(uint i);

public:
  void showMessage(std::function<void(PicoPen *pen)> message);
  void removeMessage();
  
  void showTzxOptions();
  void clearTzxOptions();
  void addTzxOption(const char *);
  void tzxOption(std::function<void(uint32_t option)> tzxOption) { _tzxOption = tzxOption; }
  void refresh(std::function<void()> refresh);
  void snapName(const char* name);
  void snapLoaded(std::function<void(const char *name)> listener) { _snapLoadedListener = listener; }
  void nextSnap(int d);
  void quickSave(int slot);
  void quickLoad(int slot);
  void setWizLayout(int32_t margin, int32_t cols1, int32_t cols2, int32_t w);
  void initialise();
  void saveSettings();
  void loadSettings();
  
  ZxSpectrumMenu(
    SdCardFatFsSpi* sdCard,
    ZxSpectrum *zxSpectrum,
    ZxSpectrumSettings *settings
  );
};
