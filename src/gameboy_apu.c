#if defined(ARDUINO_ARCH_ESP32)
#pragma GCC optimize ("O3")
#endif
#define MINIGB_APU_AUDIO_FORMAT_S16SYS
#define AUDIO_SAMPLE_RATE 22050
#include "third_party/peanut_gb/minigb_apu/minigb_apu.c"
