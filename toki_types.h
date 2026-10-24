// Shared types for toki.ino.
// They live in a header on purpose: the Arduino builder auto-generates
// function prototypes, and those prototypes must be able to see these types.
#pragma once
#include <Arduino.h>

enum Emotion : uint8_t { NEUTRAL, HAPPY, ANGRY, CURIOUS, SURPRISED, SLEEPY, SLEEPING, DIZZY, LOVE };

enum SoundId : uint8_t {
  SND_STARTUP, SND_HAPPY, SND_CURIOUS, SND_ANGRY, SND_SURPRISE,
  SND_SLEEP, SND_WAKE, SND_DIZZY, SND_LOVE, SND_BLIP,
  SND_COUNT,
  SND_NONE = 255
};

struct ToneStep { uint16_t f0, f1, ms; };   // glide f0 -> f1 over ms (f0 = 0: silence)
struct SoundDef {
  const char *name;
  const char *file;
  ToneStep steps[6];
  uint8_t nSteps;
};
