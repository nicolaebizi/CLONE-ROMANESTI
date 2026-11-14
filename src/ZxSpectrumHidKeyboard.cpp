// SPDX-License-Identifier: MIT
// Copyright (c) 2026 fruit-bat
#include "ZxSpectrumHidKeyboard.h"
#include "stdlib.h"
#include <pico/printf.h>

#define BIT0  0x01
#define BIT1  0x02
#define BIT2  0x04
#define BIT3  0x08
#define BIT4  0x10

typedef struct  {
  uint8_t line;
  uint8_t key;
} ZxSpectrumKeyContact;

typedef struct {
  uint8_t keycode;
  uint8_t contacts;
  ZxSpectrumKeyContact contact[2];
} ZxSpectrumHidKey;

ZxSpectrumHidKey KEYS[] = {
  { HID_KEY_SHIFT_LEFT,  1, { {0, BIT0} }},
  { HID_KEY_SHIFT_RIGHT, 1, { {0, BIT0} }},
  { HID_KEY_Z,           1, { {0, BIT1} }},
  { HID_KEY_X,           1, { {0, BIT2} }},
  { HID_KEY_C,           1, { {0, BIT3} }},
  { HID_KEY_V,           1, { {0, BIT4} }},

  { HID_KEY_A,           1, { {1, BIT0} }},
  { HID_KEY_S,           1, { {1, BIT1} }},
  { HID_KEY_D,           1, { {1, BIT2} }},
  { HID_KEY_F,           1, { {1, BIT3} }},
  { HID_KEY_G,           1, { {1, BIT4} }},

  { HID_KEY_Q,           1, { {2, BIT0} }},
  { HID_KEY_W,           1, { {2, BIT1} }},
  { HID_KEY_E,           1, { {2, BIT2} }},
  { HID_KEY_R,           1, { {2, BIT3} }},
  { HID_KEY_T,           1, { {2, BIT4} }},

  { HID_KEY_1,           1, { {3, BIT0} }},
  { HID_KEY_2,           1, { {3, BIT1} }},
  { HID_KEY_3,           1, { {3, BIT2} }},
  { HID_KEY_4,           1, { {3, BIT3} }},
  { HID_KEY_5,           1, { {3, BIT4} }},

  { HID_KEY_0,           1, { {4, BIT0} }},
  { HID_KEY_9,           1, { {4, BIT1} }},
  { HID_KEY_8,           1, { {4, BIT2} }},
  { HID_KEY_7,           1, { {4, BIT3} }},
  { HID_KEY_6,           1, { {4, BIT4} }},

  { HID_KEY_P,           1, { {5, BIT0} }},
  { HID_KEY_O,           1, { {5, BIT1} }},
  { HID_KEY_I,           1, { {5, BIT2} }},
  { HID_KEY_U,           1, { {5, BIT3} }},
  { HID_KEY_Y,           1, { {5, BIT4} }},

  { HID_KEY_ENTER,       1, { {6, BIT0} }},
  { HID_KEY_L,           1, { {6, BIT1} }},
  { HID_KEY_K,           1, { {6, BIT2} }},
  { HID_KEY_J,           1, { {6, BIT3} }},
  { HID_KEY_H,           1, { {6, BIT4} }},

  { HID_KEY_SPACE,       1, { {7, BIT0} }},
  { HID_KEY_ALT_RIGHT,   1, { {7, BIT1} }},
  { HID_KEY_M,           1, { {7, BIT2} }},
  { HID_KEY_N,           1, { {7, BIT3} }},
  { HID_KEY_B,           1, { {7, BIT4} }},
  
  { HID_KEY_BACKSPACE,   2, { {0, BIT0}, {4, BIT0} }},
  { HID_KEY_PAUSE,       2, { {0, BIT0}, {7, BIT0} }},
  { HID_KEY_COMMA,       2, { {7, BIT1}, {7, BIT3} }},
  { HID_KEY_PERIOD,      2, { {7, BIT1}, {7, BIT2} }},
  { HID_KEY_SLASH,       2, { {7, BIT1}, {0, BIT4} }},
  { HID_KEY_SEMICOLON,   2, { {7, BIT1}, {5, BIT1} }},
  { HID_KEY_APOSTROPHE,  2, { {7, BIT1}, {4, BIT3} }},
  { HID_KEY_MINUS,       2, { {7, BIT1}, {6, BIT3} }},
  { HID_KEY_EQUAL,       2, { {7, BIT1}, {6, BIT1} }},
  { HID_KEY_EUROPE_1,    2, { {7, BIT1}, {3, BIT2} }},

  { HID_KEY_ARROW_LEFT,  2, { {0, BIT0}, {3, BIT4} }},
  { HID_KEY_ARROW_DOWN,  2, { {0, BIT0}, {4, BIT4} }},
  { HID_KEY_ARROW_UP,    2, { {0, BIT0}, {4, BIT3} }},
  { HID_KEY_ARROW_RIGHT, 2, { {0, BIT0}, {4, BIT2} }}
};

// CoBra 1 keyboard (COBRA-KB v.1.0): the K5 column is read on data bit 5
// (CTRL on KA8 ... F4 on KA15) and the composite keys drive CS plus a key.
#define BIT5  0x20
static const ZxSpectrumHidKey COBRA_KEYS[] = {
  { HID_KEY_ESCAPE,       1, { {3, BIT5} }},             // ESC
  { HID_KEY_KEYPAD_ENTER, 1, { {2, BIT5} }},             // LF
  { HID_KEY_SCROLL_LOCK,  1, { {1, BIT5} }},             // NO SCR
  { HID_KEY_F5,           1, { {4, BIT5} }},             // F1
  { HID_KEY_F6,           1, { {5, BIT5} }},             // F2
  { HID_KEY_F7,           1, { {6, BIT5} }},             // F3
  { HID_KEY_F8,           1, { {7, BIT5} }},             // F4
  { HID_KEY_CAPS_LOCK,    2, { {0, BIT0}, {3, BIT1} }},  // CS LK = CS + 2
  { HID_KEY_HOME,         2, { {0, BIT0}, {4, BIT1} }},  // GRAPH = CS + 9
  { HID_KEY_PAGE_UP,      2, { {0, BIT0}, {3, BIT0} }},  // PG ED = CS + 1
  { HID_KEY_INSERT,       2, { {0, BIT0}, {7, BIT1} }},  // NEW M = CS + SS
  { HID_KEY_DELETE,       2, { {0, BIT0}, {4, BIT0} }},  // DEL = CS + 0
};

// CUB-Z: the VDT-52S keyboard sends ASCII codes serially, so the PC keys are
// translated to ASCII (US layout) instead of a Spectrum key matrix.
static uint8_t cubzAscii(uint8_t k, uint8_t m) {
  const bool shift = (m & 0x22) != 0;
  const bool ctrl = (m & 0x11) != 0;
  if (k >= HID_KEY_A && k <= HID_KEY_Z) {
    const uint8_t c = (uint8_t)(k - HID_KEY_A);
    if (ctrl) return (uint8_t)(c + 1);
    return (uint8_t)((shift ? 'A' : 'a') + c);
  }
  if (k >= HID_KEY_1 && k <= HID_KEY_0) {
    static const char plain[] = "1234567890";
    static const char shifted[] = "!@#$%^&*()";
    const uint8_t i = (uint8_t)(k - HID_KEY_1);
    return (uint8_t)(shift ? shifted[i] : plain[i]);
  }
  switch (k) {
    case HID_KEY_ENTER: case HID_KEY_KEYPAD_ENTER: return 13;
    case HID_KEY_ESCAPE: return 27;
    case HID_KEY_BACKSPACE: return 8;
    case HID_KEY_TAB: return 9;
    case HID_KEY_SPACE: return ' ';
    case HID_KEY_DELETE: return 127;
    case HID_KEY_MINUS: return shift ? '_' : '-';
    case HID_KEY_EQUAL: return shift ? '+' : '=';
    case HID_KEY_BRACKET_LEFT: return shift ? '{' : '[';
    case HID_KEY_BRACKET_RIGHT: return shift ? '}' : ']';
    case HID_KEY_BACKSLASH: return shift ? '|' : '\\';
    case HID_KEY_SEMICOLON: return shift ? ':' : ';';
    case HID_KEY_APOSTROPHE: return shift ? '"' : '\'';
    case HID_KEY_GRAVE: return shift ? '~' : '`';
    case HID_KEY_COMMA: return shift ? '<' : ',';
    case HID_KEY_PERIOD: return shift ? '>' : '.';
    case HID_KEY_SLASH: return shift ? '?' : '/';
    // Cursor keys as the WordStar control codes used by CP/M editors.
    case HID_KEY_ARROW_UP: return 0x05;
    case HID_KEY_ARROW_DOWN: return 0x18;
    case HID_KEY_ARROW_LEFT: return 0x13;
    case HID_KEY_ARROW_RIGHT: return 0x04;
    default: return 0;
  }
}

static bool KEYS_SORTED  = false;

static int keys_comparator(const ZxSpectrumHidKey *k1, const ZxSpectrumHidKey *k2) {
  int kk1 = (int)k1->keycode;
  int kk2 = (int)k2->keycode;
  return kk2 - kk1;
}

static int keys_v_comparator(const void *k1, const void *k2) {
  return keys_comparator((ZxSpectrumHidKey *)k1, (ZxSpectrumHidKey *)k2);
}

const static unsigned int KEYS_LEN = sizeof(KEYS) / sizeof(ZxSpectrumHidKey);

const static unsigned int KEY_SIZE = sizeof(ZxSpectrumHidKey);

static void sort_keys() {
  if (!KEYS_SORTED) {
    qsort(KEYS, KEYS_LEN, KEY_SIZE, keys_v_comparator);
  }
  KEYS_SORTED = true;
}

static ZxSpectrumHidKey* findKey(const unsigned char keycode) {
  if (keycode <= 1) return 0;
  const ZxSpectrumHidKey k0 = {keycode, 0, 0};
  return (ZxSpectrumHidKey *) bsearch(&k0, KEYS, KEYS_LEN, KEY_SIZE, keys_v_comparator);
}

static bool isInReport(hid_keyboard_report_t const *report, const unsigned char keycode) {
  for(unsigned int i = 0; i < 6; i++) {
    if (report->keycode[i] == keycode)  return true;
  }
  return false;
}

ZxSpectrumHidKeyboard::ZxSpectrumHidKeyboard(
  ZxSpectrumFileLoop *zxSpectrumSnapList,
  QuickSave* quickSave, 
  ZxSpectrumJoystick * zxSpectrumJoystick,
  ZxSpectrumMouse * zxSpectrumMouse  
) :
  ZxSpectrumKeyboard(zxSpectrumJoystick, zxSpectrumMouse),
  _zxSpectrumSnapList(zxSpectrumSnapList),
  _quickSave(quickSave),
  _kiosk(false)
{
  sort_keys();
}

ZxSpectrumHidKeyboard::ZxSpectrumHidKeyboard(
  QuickSave* quickSave, 
  ZxSpectrumJoystick * zxSpectrumJoystick,
    ZxSpectrumMouse * zxSpectrumMouse
) :
  ZxSpectrumKeyboard(zxSpectrumJoystick, zxSpectrumMouse),
  _zxSpectrumSnapList(0),
  _quickSave(quickSave),
  _kiosk(false)
{
  sort_keys();
}

static uint8_t func_keys[] = {
  HID_KEY_F1,
  HID_KEY_F2,
  HID_KEY_F3,
  HID_KEY_F4,
  HID_KEY_F5,
  HID_KEY_F6,
  HID_KEY_F7,
  HID_KEY_F8,
  HID_KEY_F9,
  HID_KEY_F10,
  HID_KEY_F11,
  HID_KEY_F12,
  HID_KEY_F13,
  HID_KEY_F14  
};

int ZxSpectrumHidKeyboard::processHidReport(hid_keyboard_report_t const *report, hid_keyboard_report_t const *prev_report) {
  int r = 0;

  reset();
  if (report->keycode[0] == 1) return r;
  const unsigned char m = report->modifier;
  // printf("hid:%02X %02X%02X%02X%02X%02X%02X\n", m, report->keycode[0], report->keycode[1], report->keycode[2], report->keycode[3], report->keycode[4], report->keycode[5]);

  if (m & 0x22) press(0, BIT0); // Shift
  if (m & 0x40) press(7, BIT1); // AtlGr -> Symbol
  const bool cobra = _ZxSpectrum && _ZxSpectrum->type() == ZxSpectrumCobra;
  const bool cubz = _ZxSpectrum && _ZxSpectrum->type() == ZxSpectrumCubz;
  if (cobra && (m & 0x11)) press(0, BIT5); // Ctrl -> CoBra CTRL

  uint32_t fkd = 0;
  uint32_t fkp = 0;
  
  for(unsigned int i = 0; i < 6; ++i) {
    const unsigned char hidKeyCode = report->keycode[i];
    
    for (unsigned int fk = 0; fk < sizeof(func_keys); ++fk) {
      const uint32_t fkb = 1 << fk;
      const unsigned char fkc = func_keys[fk];
      if (hidKeyCode == fkc) {
        fkd |= fkb;
        if (!isInReport(prev_report, fkc)) fkp |= fkb;
      }
    }

    if (cubz && hidKeyCode > 1 && !isInReport(prev_report, hidKeyCode)) {
      const uint8_t a = cubzAscii(hidKeyCode, m);
      if (a) _ZxSpectrum->cubzKey(a);
    }

    if (cobra) {
      for (unsigned int c = 0; c < sizeof(COBRA_KEYS) / sizeof(COBRA_KEYS[0]); ++c) {
        const ZxSpectrumHidKey *ck = &COBRA_KEYS[c];
        if (ck->keycode != hidKeyCode) continue;
        for (uint32_t j = 0; j < ck->contacts; ++j) press(ck->contact[j].line, ck->contact[j].key);
      }
    }

    const ZxSpectrumHidKey *k = findKey(hidKeyCode);
    if (k) {
      for (uint32_t c = 0; c < k->contacts; ++c) {
        const ZxSpectrumKeyContact *contact = &k->contact[c];
        press(contact->line, contact->key);
      }
    }
  }

  if ((m & HID_KEY_MOD_SAVE) && !_kiosk) {
    for (int i = 0; i < 12; ++i) {
      if (fkp & (1 << i)) {
        if (_quickSave) _quickSave->save(_ZxSpectrum, i);
      }
    }
  }
  else if (m & HID_KEY_MOD_LOAD) {
    for (int i = 0; i < 12; ++i) {
      if (fkp & (1 << i)) {
        if (_quickSave) _quickSave->load(_ZxSpectrum, i);
      }
    }
  }
  else {
    // F1 open menu
    if ((fkp & (1 << 0)) && !_kiosk) r = 1;
    
    // F3 toggle mute
    if ((fkp & (1 << 2))) _ZxSpectrum->toggleMute();    
    
    if ((fkp & (1 << 12)) && _quickSave) _quickSave->save(_ZxSpectrum, 0);
    if ((fkp & (1 << 13)) && _quickSave) _quickSave->load(_ZxSpectrum, 0);

    // F11 reset to 48k
    if (fkp & (1 << 10)) _ZxSpectrum->reset(ZxSpectrum48k);
    // F11 reset to 128k
    if (fkp & (1 << 11)) _ZxSpectrum->reset(ZxSpectrum128k);
    // F4 toggle moderate
    if (fkp & (1 << 3)) _ZxSpectrum->toggleModerate();
    
    if (_zxSpectrumSnapList) {
      // F8 curr snap (on the CoBra F5-F8 are its F1-F4 keys)
      if ((fkp & (1 << 7)) && !cobra) _zxSpectrumSnapList->curr(_ZxSpectrum);
      // F9 previous snap
      if (fkp & (1 << 8)) _zxSpectrumSnapList->prev(_ZxSpectrum);
      // F10 next snap
      if (fkp & (1 << 9)) _zxSpectrumSnapList->next(_ZxSpectrum);
    }
  }
  
  return r;
}
