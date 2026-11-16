// Host-test stub for the SD-card/FatFs mount provider behind ZxSpectrumCf:
// the test drives mounted()/mount() through the fake* flags.
#pragma once

class SdCardFatFsSpi {
public:
  bool mounted() { return fakeMounted; }
  bool mount() { fakeMounted = fakeMountOk; return fakeMountOk; }
  bool fakeMounted = false;
  bool fakeMountOk = true;
};
