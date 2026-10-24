/*
  ============================================================
   TOKI v1.0  -  a tiny robot with feelings
  ============================================================
  Board    : Seeed XIAO ESP32-S3 (ESP32 Arduino core 3.x)
  Libraries: Adafruit GFX Library, Adafruit SH110X
             (audio + microphone use the built-in ESP_I2S library,
              so the old ESP8266Audio library is NOT needed any more)

  Wiring
    OLED SH1106 (I2C)  SDA -> D4 (GPIO5)   SCL -> D5 (GPIO6)   addr 0x3C
    MPU6050   (I2C)    SDA -> D4 (GPIO5)   SCL -> D5 (GPIO6)   addr 0x69 (AD0 -> 3V3)
    I2S amp (MAX98357) BCLK -> D9 (GPIO8)  LRC -> D8 (GPIO7)   DIN -> D10 (GPIO9)
    INMP441 mic        SCK -> D0 (GPIO1)   WS -> D1 (GPIO2)    SD -> D2 (GPIO3)   L/R -> GND
    Button             the BOOT button on the XIAO (GPIO0)

  Sounds: put WAV files in a "data" folder next to this sketch and upload
  with the LittleFS uploader. See README.md for the file names. If a file
  is missing Toki beeps and chirps instead, so it always works.

  Volume and mute are remembered across reboots.

  Serial Monitor (115200): type "help" for test commands.
*/

#include <Arduino.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SH110X.h>
#include <LittleFS.h>
#include <ESP_I2S.h>
#include <Preferences.h>
#include "toki_types.h"

// ============================================================
//  CONFIG  (everything you might want to tune lives here)
// ============================================================

// ---- Pins ----
#define PIN_SDA        5      // D4
#define PIN_SCL        6      // D5
#define PIN_AMP_BCLK   8      // D9
#define PIN_AMP_LRC    7      // D8
#define PIN_AMP_DIN    9      // D10
#define PIN_MIC_SCK    1      // D0
#define PIN_MIC_WS     2      // D1
#define PIN_MIC_SD     3      // D2
#define PIN_BUTTON     0      // BOOT button

// ---- I2C ----
#define I2C_CLOCK      400000
#define OLED_ADDR      0x3C
#define MPU_ADDR       0x69

// ---- Behaviour ----
#define DEFAULT_VOLUME     0.5f      // 0.0 - 1.0
#define FRAME_MS           33        // ~30 fps
#define BOOT_MS            3000UL
#define SLEEP_AFTER_MS     60000UL   // idle time before Toki falls asleep (0 = never)
#define SLEEPY_MS          4000UL    // drowsy phase before sleeping
#define SHAKE_DIZZY_MS     3500UL    // shake this long and Toki gets dizzy
#define IDLE_EVENT_MIN_MS  20000UL   // random "curious / happy" moments
#define IDLE_EVENT_MAX_MS  45000UL

// ---- Motion sensor orientation ----
// UP_AXIS: which MPU6050 axis reads +1 g when Toki stands the way you
// normally hold him (0=X, 1=Y, 2=Z). Use the serial command "imu" to see.
// UP_SIGN: +1 or -1 so that "upright" reads +1 g.
#define UP_AXIS        2
#define UP_SIGN        1
// If the eyes follow the tilt the wrong way, flip these.
#define LOOK_X_SIGN    1
#define LOOK_Y_SIGN    1
#define AUTO_ROTATE    1      // flip the screen when Toki is turned upside down

// ---- Motion thresholds (g) ----
#define JOLT_G         1.3f   // sudden bump
#define FALL_G         0.35f  // free fall
#define SHAKE_ON       0.70f
#define SHAKE_OFF      0.35f

// ---- Microphone thresholds (see "mic" serial command) ----
#define MIC_SHIFT        14      // 32-bit sample >> 14
#define MIC_MIN_BASE     8.0f
#define MIC_LOUD_RATIO   6.0f
#define MIC_LOUD_MIN     1500.0f
#define MIC_VOICE_RATIO  2.5f
#define MIC_VOICE_MIN    120.0f

// ---- Face geometry ----
#define SCREEN_WIDTH   128
#define SCREEN_HEIGHT  64
#define EYE_RADIUS     12
#define LEFT_EYE_X     35
#define RIGHT_EYE_X    93
#define EYE_Y          32

// ============================================================
//  TYPES
// ============================================================

//                                   NEUT HAPPY ANGRY CUR SURP SLPY SLP DIZZY LOVE
static const uint8_t EMO_PRIO[9] = {  0,   2,    4,    1,   3,   1,   1,   3,    2 };

// WAV file name + fallback chirp for every sound
static const SoundDef SOUNDS[SND_COUNT] = {
  { "startup",  "/startup_alt.wav",  { {400, 800, 120}, {800, 600, 80}, {600, 1200, 160} }, 3 },
  { "happy",    "/happy.wav",        { {700, 1100, 90}, {1100, 900, 60}, {900, 1400, 120} }, 3 },
  { "curious",  "/curious.wav",      { {500, 900, 140}, {0, 0, 60}, {700, 1100, 160} }, 3 },
  { "angry",    "/angry2.wav",       { {300, 200, 140}, {0, 0, 40}, {300, 180, 200} }, 3 },
  { "surprise", "/surprise_1.wav",   { {600, 1600, 110}, {1600, 1500, 100} }, 2 },
  { "sleep",    "/sleep_toki.wav",   { {700, 400, 300}, {400, 250, 500} }, 2 },
  { "wake",     "/wake_up_2.wav",    { {300, 600, 180}, {600, 900, 160} }, 2 },
  { "dizzy",    "/dizzy.wav",        { {500, 900, 90}, {900, 400, 90}, {400, 900, 90}, {900, 300, 120} }, 4 },
  { "love",     "/love.wav",         { {800, 1000, 100}, {0, 0, 40}, {1000, 1300, 160} }, 3 },
  { "blip",     "/blip.wav",         { {900, 1100, 50} }, 1 },
};

// ============================================================
//  GLOBALS
// ============================================================

Adafruit_SH1106G display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, -1);

static bool oledOK = false;
static bool fsOK   = false;

static inline float clampf(float v, float lo, float hi) { return v < lo ? lo : (v > hi ? hi : v); }
static inline void approach(float &cur, float target, float k) {
  cur += (target - cur) * k;
  if (fabsf(target - cur) < 0.05f) cur = target;
}
static bool takeFlag(volatile bool &f) { bool v = f; f = false; return v; }

// ============================================================
//  AUDIO  (runs in its own task on core 0, so animation never stutters)
//  Plays 16-bit PCM WAV files from LittleFS. Falls back to chirps.
// ============================================================

static I2SClass spk;
static uint32_t spkRate = 0;
static QueueHandle_t soundQ = nullptr;

static volatile bool     gStop     = false;
static volatile bool     gSpeaking = false;
static volatile uint32_t gSpeakEnd = 0;
static volatile float    gVolume   = DEFAULT_VOLUME;
static volatile bool     gMuted    = false;
static Preferences       prefs;

static void saveSettings() {
  prefs.putFloat("vol", gVolume);
  prefs.putBool("mute", gMuted);
}

static bool spkStart(uint32_t rate) {
  if (spkRate == rate) return true;
  if (spkRate) spk.end();
  spkRate = 0;
  spk.setPins(PIN_AMP_BCLK, PIN_AMP_LRC, PIN_AMP_DIN);
  if (!spk.begin(I2S_MODE_STD, rate, I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_STEREO)) {
    Serial.println("[audio] I2S start failed");
    return false;
  }
  spkRate = rate;
  return true;
}

static void spkSilence() {
  static int16_t z[256];
  memset(z, 0, sizeof(z));
  spk.write((const uint8_t *)z, sizeof(z));   // short tail of silence avoids pops
}

// returns true if the file was a valid WAV and playback was started
static bool playWav(const char *path) {
  File f = LittleFS.open(path, "r");
  if (!f) return false;

  uint8_t hdr[12];
  if (f.read(hdr, 12) != 12 || memcmp(hdr, "RIFF", 4) || memcmp(hdr + 8, "WAVE", 4)) {
    Serial.printf("[audio] %s is not a WAV file\n", path);
    f.close();
    return false;
  }

  uint16_t fmt = 0, ch = 0, bits = 0;
  uint32_t rate = 0, dataLen = 0;
  bool haveFmt = false;

  while (f.available()) {
    uint8_t ck[8];
    if (f.read(ck, 8) != 8) break;
    uint32_t sz = ck[4] | (ck[5] << 8) | (ck[6] << 16) | ((uint32_t)ck[7] << 24);
    if (!memcmp(ck, "fmt ", 4)) {
      uint8_t fb[16];
      if (sz < 16 || f.read(fb, 16) != 16) break;
      fmt  = fb[0] | (fb[1] << 8);
      ch   = fb[2] | (fb[3] << 8);
      rate = fb[4] | (fb[5] << 8) | (fb[6] << 16) | ((uint32_t)fb[7] << 24);
      bits = fb[14] | (fb[15] << 8);
      haveFmt = true;
      uint32_t rest = (sz - 16) + (sz & 1);
      if (rest) f.seek(f.position() + rest);
    } else if (!memcmp(ck, "data", 4)) {
      dataLen = sz;
      break;
    } else {
      f.seek(f.position() + sz + (sz & 1));
    }
  }

  if (!haveFmt || !dataLen || (fmt != 1 && fmt != 0xFFFE) || bits != 16 || (ch != 1 && ch != 2)) {
    Serial.printf("[audio] %s: need 16-bit PCM mono/stereo WAV\n", path);
    f.close();
    return false;
  }
  uint32_t remain = f.size() - f.position();
  if (dataLen > remain) dataLen = remain;

  if (!spkStart(rate)) { f.close(); return false; }

  static int16_t inBuf[512];
  static int16_t outBuf[1024];
  const size_t frameBytes = ch * 2;

  gSpeaking = true;
  while (dataLen && !gStop) {
    size_t want = sizeof(inBuf);
    if (want > dataLen) want = dataLen;
    want -= want % frameBytes;
    if (!want) break;
    int n = f.read((uint8_t *)inBuf, want);
    if (n <= 0) break;
    dataLen -= n;
    size_t frames = n / frameBytes;
    int32_t vol = (int32_t)(gVolume * 256.0f);
    if (ch == 1) {
      for (size_t i = 0; i < frames; i++) {
        int16_t s = (int16_t)(((int32_t)inBuf[i] * vol) >> 8);
        outBuf[2 * i] = s;
        outBuf[2 * i + 1] = s;
      }
    } else {
      for (size_t i = 0; i < frames * 2; i++) outBuf[i] = (int16_t)(((int32_t)inBuf[i] * vol) >> 8);
    }
    spk.write((const uint8_t *)outBuf, frames * 4);
  }
  spkSilence();
  f.close();
  return true;
}

// built-in chirps (used when the WAV is missing)
static void playTones(const SoundDef &s) {
  if (!spkStart(16000)) return;
  static int16_t out[512];   // 256 stereo frames
  const float vol = gVolume;
  float phase = 0;
  gSpeaking = true;
  for (uint8_t i = 0; i < s.nSteps && !gStop; i++) {
    const ToneStep &st = s.steps[i];
    uint32_t total = (uint32_t)st.ms * 16;
    uint32_t done = 0;
    while (done < total && !gStop) {
      uint32_t n = total - done;
      if (n > 256) n = 256;
      for (uint32_t k = 0; k < n; k++) {
        float u = (float)(done + k) / (float)total;
        int16_t v = 0;
        if (st.f0) {
          float f = st.f0 + ((float)st.f1 - (float)st.f0) * u;
          float env = (u < 0.1f) ? (u / 0.1f) : ((u > 0.8f) ? ((1.0f - u) / 0.2f) : 1.0f);
          phase += 6.2831853f * f / 16000.0f;
          if (phase > 6.2831853f) phase -= 6.2831853f;
          v = (int16_t)(sinf(phase) * env * 8000.0f * vol);
        }
        out[2 * k] = v;
        out[2 * k + 1] = v;
      }
      spk.write((const uint8_t *)out, n * 4);
      done += n;
    }
  }
  spkSilence();
}

static void audioTask(void *) {
  uint8_t id;
  for (;;) {
    if (xQueueReceive(soundQ, &id, portMAX_DELAY) != pdTRUE) continue;
    if (id >= SND_COUNT || gMuted) continue;
    gStop = false;
    const SoundDef &s = SOUNDS[id];
    bool ok = false;
    if (fsOK) ok = playWav(s.file);
    if (!ok && !gStop) playTones(s);
    gSpeakEnd = millis() + 250;
    gSpeaking = false;
  }
}

static void playSound(SoundId id) {
  if (id == SND_NONE || gMuted || !soundQ) return;
  gSpeakEnd = millis() + 400;   // mic ignores itself right away
  gStop = true;                 // interrupt whatever is playing
  uint8_t v = (uint8_t)id;
  xQueueOverwrite(soundQ, &v);  // latest request wins
}

static void stopSound() {
  gStop = true;
  if (soundQ) xQueueReset(soundQ);
}

// ============================================================
//  MICROPHONE  (INMP441, own task on core 0)
// ============================================================

static I2SClass mic;
static bool micOK = false;
static volatile float    micRms = 0, micBase = 100, micRatio = 1;
static volatile bool     micLoud = false;
static volatile uint32_t micVoiceMs = 0;

static float micNorm() {   // 0..1 "how loud is it right now"
  if (!micOK) return 0;
  return clampf((micRatio - 1.0f) / 5.0f, 0.0f, 1.0f);
}

static void micTask(void *) {
  static int32_t buf[256];
  uint32_t lastLoud = 0, voiceStart = 0, lastVoice = 0;
  int warm = 0;
  float base = 100;

  for (;;) {
    size_t got = mic.readBytes((char *)buf, sizeof(buf));
    int n = (int)(got / sizeof(int32_t));
    if (n <= 0) { vTaskDelay(pdMS_TO_TICKS(5)); continue; }

    int64_t sum = 0;
    for (int i = 0; i < n; i++) { buf[i] >>= MIC_SHIFT; sum += buf[i]; }
    float mean = (float)sum / (float)n;
    double sq = 0;
    for (int i = 0; i < n; i++) { double v = (double)buf[i] - mean; sq += v * v; }
    float rms = sqrtf((float)(sq / n));
    uint32_t now = millis();
    micRms = rms;

    if (warm < 100) {                       // first ~1.5 s: learn the room's noise level
      base += (rms - base) * 0.1f;
      warm++;
      micBase = base;
      continue;
    }

    bool quiet = !gSpeaking && now > gSpeakEnd;   // don't listen to our own speaker
    if (quiet) {
      if (rms < base * 1.3f) base += (rms - base) * 0.01f;
      else                   base += (rms - base) * 0.0005f;
      if (base < MIC_MIN_BASE) base = MIC_MIN_BASE;
    }
    micBase = base;

    float ratio = quiet ? (rms / base) : 1.0f;
    micRatio = micRatio * 0.7f + ratio * 0.3f;

    if (quiet && ratio > MIC_LOUD_RATIO && rms > MIC_LOUD_MIN && now - lastLoud > 1500) {
      micLoud = true;
      lastLoud = now;
    }

    if (quiet && micRatio > MIC_VOICE_RATIO && rms > MIC_VOICE_MIN) {
      if (!voiceStart) voiceStart = now;
      lastVoice = now;
    }
    if (voiceStart && now - lastVoice > 400) voiceStart = 0;   // short pauses in speech are fine
    micVoiceMs = voiceStart ? (now - voiceStart) : 0;
  }
}

static void micInit() {
  mic.setPins(PIN_MIC_SCK, PIN_MIC_WS, -1, PIN_MIC_SD);
  if (!mic.begin(I2S_MODE_STD, 16000, I2S_DATA_BIT_WIDTH_32BIT, I2S_SLOT_MODE_MONO, I2S_STD_SLOT_LEFT)) {
    Serial.println("[mic] INMP441 failed to start - mic reactions disabled");
    return;
  }
  micOK = true;
  xTaskCreatePinnedToCore(micTask, "mic", 4096, nullptr, 2, nullptr, 0);
  Serial.println("[mic] INMP441 OK");
}

// ============================================================
//  MOTION  (MPU6050, raw registers, no library)
// ============================================================

static bool mpuWrite(uint8_t reg, uint8_t val) {
  Wire.beginTransmission(MPU_ADDR);
  Wire.write(reg);
  Wire.write(val);
  return Wire.endTransmission() == 0;
}

static bool mpuRead(uint8_t reg, uint8_t *buf, uint8_t len) {
  Wire.beginTransmission(MPU_ADDR);
  Wire.write(reg);
  if (Wire.endTransmission(false) != 0) return false;
  if (Wire.requestFrom((uint8_t)MPU_ADDR, len) != len) return false;
  for (uint8_t i = 0; i < len; i++) buf[i] = Wire.read();
  return true;
}

struct Imu {
  bool ok = false;
  float a[3] = {0, 0, 1};
  float g[3] = {0, 0, 0};
  float gOff[3] = {0, 0, 0};
  float mag = 1, shake = 0, up = 1, p = 0, q = 0;
  float lookX = 0, lookY = 0;          // smoothed gaze direction, -1..1
  bool shaking = false, flipped = false;
  volatile bool evJolt = false, evFall = false, evHandled = false;
  uint32_t last = 0, aboveSince = 0, lastJolt = 0, lastFall = 0, fallSince = 0, flipSince = 0;
  float refP = 0, refQ = 0;
};
static Imu imu;

static bool imuSample(float a[3], float g[3]) {
  uint8_t d[14];
  if (!mpuRead(0x3B, d, 14)) return false;
  int16_t r[7];
  for (int i = 0; i < 7; i++) r[i] = (int16_t)((d[2 * i] << 8) | d[2 * i + 1]);
  a[0] = r[0] / 8192.0f;  a[1] = r[1] / 8192.0f;  a[2] = r[2] / 8192.0f;   // +-4 g
  g[0] = r[4] / 65.5f;    g[1] = r[5] / 65.5f;    g[2] = r[6] / 65.5f;     // +-500 dps
  return true;
}

static void imuInit() {
  uint8_t who = 0;
  if (!mpuRead(0x75, &who, 1)) {
    Serial.printf("[imu] MPU6050 not found at 0x%02X - motion reactions disabled\n", MPU_ADDR);
    return;
  }
  mpuWrite(0x6B, 0x00); delay(50);   // wake up
  mpuWrite(0x6B, 0x01);              // clock from gyro PLL
  mpuWrite(0x1A, 0x02);              // low-pass filter ~94 Hz
  mpuWrite(0x19, 0x04);              // 200 Hz internal rate
  mpuWrite(0x1B, 0x08);              // gyro +-500 dps
  mpuWrite(0x1C, 0x08);              // accel +-4 g
  delay(100);

  // gyro zero offset (keep Toki still for the first second)
  float sum[3] = {0, 0, 0}, a[3], g[3];
  int n = 0;
  for (int i = 0; i < 100; i++) {
    if (imuSample(a, g)) { for (int k = 0; k < 3; k++) sum[k] += g[k]; n++; }
    delay(4);
  }
  if (n > 50) {
    float avg[3] = { sum[0] / n, sum[1] / n, sum[2] / n };
    if (fabsf(avg[0]) < 20 && fabsf(avg[1]) < 20 && fabsf(avg[2]) < 20)
      for (int k = 0; k < 3; k++) imu.gOff[k] = avg[k];
  }
  imu.ok = true;
  Serial.printf("[imu] MPU6050 OK (WHO_AM_I=0x%02X)\n", who);
}

static void imuUpdate(uint32_t now) {
  if (!imu.ok) return;
  float a[3], g[3];
  if (!imuSample(a, g)) return;

  for (int i = 0; i < 3; i++) { imu.a[i] = a[i]; imu.g[i] = g[i] - imu.gOff[i]; }
  float gm = sqrtf(imu.g[0] * imu.g[0] + imu.g[1] * imu.g[1] + imu.g[2] * imu.g[2]);
  imu.mag = sqrtf(a[0] * a[0] + a[1] * a[1] + a[2] * a[2]);
  float dev = fabsf(imu.mag - 1.0f);

  float dt = imu.last ? (float)(now - imu.last) : 30.0f;
  imu.last = now;

  // shake energy: smoothed acceleration deviation + a bit of rotation
  float al = clampf(dt / 120.0f, 0.0f, 1.0f);
  float prevShake = imu.shake;
  imu.shake += ((dev + gm / 800.0f) - imu.shake) * al;

  // sudden bump
  if (dev > JOLT_G && prevShake < 0.35f && now - imu.lastJolt > 1200) {
    imu.evJolt = true;
    imu.lastJolt = now;
  }
  // free fall
  if (imu.mag < FALL_G) {
    if (!imu.fallSince) imu.fallSince = now;
    else if (now - imu.fallSince > 120 && now - imu.lastFall > 2500) { imu.evFall = true; imu.lastFall = now; }
  } else if (imu.mag > 0.6f) {
    imu.fallSince = 0;
  }
  // shaking (needs to be sustained for 200 ms)
  if (imu.shake > SHAKE_ON) {
    if (!imu.aboveSince) imu.aboveSince = now;
    if (now - imu.aboveSince > 200) imu.shaking = true;
  } else if (imu.shake < SHAKE_OFF) {
    imu.aboveSince = 0;
    imu.shaking = false;
  }

  // orientation
  const int ua = UP_AXIS;
  const int pi = (ua == 0) ? 1 : 0;
  const int qi = (ua == 2) ? 1 : 2;
  imu.up = UP_SIGN * imu.a[ua];
  imu.p = imu.a[pi];
  imu.q = imu.a[qi];

  bool wantFlip = imu.up < -0.7f;
  bool wantNormal = imu.up > 0.3f;
  if ((wantFlip && !imu.flipped) || (wantNormal && imu.flipped)) {
    if (!imu.flipSince) imu.flipSince = now;
    else if (now - imu.flipSince > 800) { imu.flipped = wantFlip; imu.flipSince = 0; }
  } else {
    imu.flipSince = 0;
  }

  // gaze follows tilt (eyes look "downhill")
  float s = imu.flipped ? -1.0f : 1.0f;
  float tx = clampf(-LOOK_X_SIGN * imu.p / 0.45f, -1.0f, 1.0f) * s;
  float ty = clampf(-LOOK_Y_SIGN * imu.q / 0.45f, -1.0f, 1.0f) * s;
  float la = clampf(dt / 250.0f, 0.0f, 1.0f);
  imu.lookX += (tx - imu.lookX) * la;
  imu.lookY += (ty - imu.lookY) * la;

  // "someone is handling me" (small movements or tilt changes)
  if (fabsf(imu.p - imu.refP) > 0.12f || fabsf(imu.q - imu.refQ) > 0.12f || dev > 0.15f) {
    imu.evHandled = true;
    imu.refP = imu.p;
    imu.refQ = imu.q;
  }
}

// ============================================================
//  BUTTON  (BOOT button): click = pet, double click = love, hold = mute
// ============================================================

struct Btn {
  bool down = false, longFired = false, pending = false;
  uint32_t downAt = 0, lastClick = 0;
  volatile bool evClick = false, evDouble = false, evLong = false;
};
static Btn btn;

static void buttonUpdate(uint32_t now) {
  bool pressed = digitalRead(PIN_BUTTON) == LOW;
  if (pressed && !btn.down) {
    btn.down = true; btn.downAt = now; btn.longFired = false;
  } else if (pressed && btn.down) {
    if (!btn.longFired && now - btn.downAt > 1500) { btn.longFired = true; btn.pending = false; btn.evLong = true; }
  } else if (!pressed && btn.down) {
    btn.down = false;
    if (!btn.longFired && now - btn.downAt > 30) {
      if (btn.pending && now - btn.lastClick < 400) { btn.pending = false; btn.evDouble = true; }
      else { btn.pending = true; btn.lastClick = now; }
    }
  }
  if (btn.pending && !btn.down && now - btn.lastClick > 400) { btn.pending = false; btn.evClick = true; }
}

// ============================================================
//  FACE
// ============================================================

struct Face {
  float open = 0, openEff = 0;
  float offX = 0, offY = 0, pupX = 0, pupY = 0, pupR = 5, conv = 0, jitter = 0;
  bool blinking = false;
  uint32_t blinkStart = 0, nextBlink = 2500;
};
static Face face;

// current emotion state
static Emotion  cur = NEUTRAL;
static uint32_t emoStart = 0, emoUntil = 0;
static bool     emoFlag = false;
static bool     booting = true, bootBlink = false;
static uint32_t bootStart = 0;
static uint32_t muteShowUntil = 0;

// eyebrows: left polyline, right one is the mirror image
static const int8_t BROW_NEUTRAL[4][2]   = { {18, 22}, {24, 18}, {30, 15}, {35, 17} };
static const int8_t BROW_HAPPY[4][2]     = { {18, 19}, {24, 15}, {30, 12}, {35, 14} };
static const int8_t BROW_SURPRISED[4][2] = { {18, 16}, {24, 11}, {30, 8},  {35, 10} };
static const int8_t BROW_SLEEP[7][2]     = { {18, 22}, {21, 19}, {24, 17}, {27, 16}, {30, 17}, {33, 19}, {35, 22} };
static const int8_t BROW_ANGRY[2][2]     = { {23, 18}, {37, 24} };
static const int8_t BROW_CURIOUS_R[4][2] = { {93, 12}, {98, 9}, {104, 11}, {110, 16} };   // right side, not mirrored

static void polyBrow(const int8_t (*pts)[2], int n, bool mirror, int dx, int dy) {
  for (int th = 0; th < 3; th++) {
    for (int i = 0; i < n - 1; i++) {
      int x0 = pts[i][0], y0 = pts[i][1] + th;
      int x1 = pts[i + 1][0], y1 = pts[i + 1][1] + th;
      if (mirror) { x0 = 128 - x0; x1 = 128 - x1; }
      display.drawLine(x0 + dx, y0 + dy, x1 + dx, y1 + dy, SH110X_WHITE);
    }
  }
}

static void drawBrows(Emotion e, int dy, int dx) {
  switch (e) {
    case HAPPY:
    case LOVE:
      polyBrow(BROW_HAPPY, 4, false, dx, dy);  polyBrow(BROW_HAPPY, 4, true, dx, dy);
      break;
    case SURPRISED:
      polyBrow(BROW_SURPRISED, 4, false, dx, dy);  polyBrow(BROW_SURPRISED, 4, true, dx, dy);
      break;
    case ANGRY:
      polyBrow(BROW_ANGRY, 2, false, dx, dy);  polyBrow(BROW_ANGRY, 2, true, dx, dy);
      break;
    case CURIOUS:
      polyBrow(BROW_NEUTRAL, 4, false, dx, dy);
      polyBrow(BROW_CURIOUS_R, 4, false, dx, dy);
      break;
    case SLEEPING:
      polyBrow(BROW_SLEEP, 7, false, dx, dy);  polyBrow(BROW_SLEEP, 7, true, dx, dy);
      break;
    case SLEEPY:
      polyBrow(BROW_NEUTRAL, 4, false, dx, dy + 1);  polyBrow(BROW_NEUTRAL, 4, true, dx, dy + 1);
      break;
    default:
      polyBrow(BROW_NEUTRAL, 4, false, dx, dy);  polyBrow(BROW_NEUTRAL, 4, true, dx, dy);
      break;
  }
}

static void drawEye(int cx, int cy, int open, int px, int py, int pr, bool pupil) {
  const int R = EYE_RADIUS;
  if (open <= 1) {                                    // closed eye = a line
    display.fillRect(cx - 9, cy - 1, 18, 3, SH110X_WHITE);
    return;
  }
  display.fillCircle(cx, cy, R, SH110X_WHITE);
  if (open < R) {                                     // eyelids
    display.fillRect(cx - R, cy - R, 2 * R + 1, R - open, SH110X_BLACK);
    display.fillRect(cx - R, cy + open, 2 * R + 1, R - open + 1, SH110X_BLACK);
  }
  if (pupil && open > 3) display.fillCircle(cx + px, cy + py, pr, SH110X_BLACK);
}

static void drawHappyEye(int cx, int cy) {            // ^ shaped eye
  drawEye(cx, cy, 5, 0, 0, 0, false);
  for (int k = 0; k < 2; k++) {
    display.drawLine(cx - 4, cy + 2 + k, cx, cy - 2 + k, SH110X_BLACK);
    display.drawLine(cx, cy - 2 + k, cx + 4, cy + 2 + k, SH110X_BLACK);
  }
}

static void drawHeart(int cx, int cy, float s) {
  int r = (int)(5 * s + 0.5f);
  int off = (int)(4 * s + 0.5f);
  display.fillCircle(cx - off, cy - 3, r, SH110X_WHITE);
  display.fillCircle(cx + off, cy - 3, r, SH110X_WHITE);
  display.fillTriangle(cx - off - r + 1, cy - 1, cx + off + r - 1, cy - 1, cx, cy + (int)(11 * s), SH110X_WHITE);
}

static void drawSleepZ(int x, int y) {
  display.drawLine(x, y, x + 4, y, SH110X_WHITE);
  display.drawLine(x + 4, y, x, y + 5, SH110X_WHITE);
  display.drawLine(x, y + 5, x + 4, y + 5, SH110X_WHITE);
}

static void drawSleeping(uint32_t now) {
  int breath = ((now / 500) % 2) ? 1 : 0;
  int ly = EYE_Y + breath;
  const int xs[2] = { LEFT_EYE_X, RIGHT_EYE_X };
  for (int i = 0; i < 2; i++) {                       // \____/ closed eyes
    int x = xs[i];
    display.drawLine(x - 8, ly, x - 3, ly + 2, SH110X_WHITE);
    display.drawLine(x - 3, ly + 2, x + 3, ly + 2, SH110X_WHITE);
    display.drawLine(x + 3, ly + 2, x + 8, ly, SH110X_WHITE);
  }
  drawBrows(SLEEPING, breath, 0);

  uint32_t step = (now - emoStart) / 80;              // floating Zzz
  for (int k = 0; k < 3; k++) {
    int s = (int)((step + k * 10) % 32);
    int zx = 110 - s / 2 + ((s % 6 < 3) ? 1 : -1);
    int zy = 26 - s;
    if (zy >= 0) drawSleepZ(zx, zy);
  }
}

static void startBlink(uint32_t now) {
  if (!face.blinking) { face.blinking = true; face.blinkStart = now; }
}

// where Toki looks while calm: follows tilt, otherwise glances around
static float glanceX = 0, glanceY = 0;
static uint32_t glanceUntil = 0, nextGlance = 3000;

static void computeGaze(uint32_t now, float &lx, float &ly) {
  lx = imu.lookX;
  ly = imu.lookY;
  if (!imu.ok || (fabsf(lx) < 0.2f && fabsf(ly) < 0.2f)) {
    if (now >= nextGlance) {
      int r = random(5);
      glanceX = (r == 0) ? -1.0f : (r == 1) ? 1.0f : 0.0f;
      glanceY = (r == 2) ? -0.6f : (r == 3) ? 0.6f : 0.0f;
      glanceUntil = now + random(600, 1400);
      nextGlance = now + random(2500, 7000);
    }
    if (now < glanceUntil) { lx = glanceX; ly = glanceY; }
    else { lx = 0; ly = 0; }
  }
}

static void faceUpdate(uint32_t now) {
  const uint32_t t = now - emoStart;
  float tOpen = 12, tOffX = 0, tOffY = 0, tPupX = 0, tPupY = 0, tPupR = 5, tConv = 0, jit = 0;
  float kOpen = 0.35f, kLook = 0.3f;
  bool canBlink = true;
  float lx = 0, ly = 0;

  if (booting) {                                      // startup: eyes open, look around, blink
    uint32_t tb = now - bootStart;
    canBlink = false;
    kOpen = 0.12f;
    tOpen = (tb < 200) ? 0 : 12;
    if (tb > 1100 && tb < 1600) lx = -1;
    else if (tb >= 1600 && tb < 2100) lx = 1;
    if (tb > 2400 && !bootBlink) { bootBlink = true; startBlink(now); }
    tOffX = lx * 3; tPupX = lx * 5;
  } else {
    switch (cur) {
      case NEUTRAL:
        computeGaze(now, lx, ly);
        tOffX = lx * 3; tOffY = ly * 3; tPupX = lx * 5; tPupY = ly * 5;
        tPupR = 5.0f + 2.0f * micNorm();              // pupils pulse with sound
        break;
      case CURIOUS:
        if (t < 800) lx = -1;
        else if (t < 1600) ly = -1;
        else if (t < 2400) lx = 1;
        tOffX = lx * 3; tOffY = ly * 3; tPupX = lx * 5; tPupY = ly * 5;
        break;
      case HAPPY:
        tOpen = 5; tOffY = -2.0f + 1.5f * sinf(t * 0.026f); kLook = 0.5f; canBlink = false;
        break;
      case ANGRY:
        tPupR = 3; tConv = 2; jit = ((t / 110) % 2) ? 1.0f : -1.0f;
        break;
      case SURPRISED:
        tPupR = 7;
        if (t > 400 && !emoFlag) { emoFlag = true; startBlink(now); }
        break;
      case SLEEPY:
        tOpen = 4; tPupY = 5; kOpen = 0.08f; canBlink = false;
        if ((t > 800 && t < 1100) || (t > 2300 && t < 2600)) tOpen = 2;   // slow blinks
        break;
      case SLEEPING:
        tOpen = 2; canBlink = false;
        break;
      case DIZZY: {
        float a = t * 0.012f;
        tPupX = cosf(a) * 5; tPupY = sinf(a) * 5; tOffX = sinf(t * 0.02f) * 2;
        kLook = 0.8f; canBlink = false;
        break;
      }
      case LOVE:
        tOffY = -1.0f + sinf(t * 0.01f); kLook = 0.5f; canBlink = false;
        break;
    }
  }

  // automatic blinking
  if (canBlink && !face.blinking && now >= face.nextBlink) {
    startBlink(now);
    face.nextBlink = now + random(2200, 5500);
    if (random(100) < 15) face.nextBlink = now + 260;   // occasional double blink
  }

  approach(face.open, tOpen, kOpen);
  approach(face.offX, tOffX, kLook);
  approach(face.offY, tOffY, kLook);
  approach(face.pupX, tPupX, kLook);
  approach(face.pupY, tPupY, kLook);
  approach(face.pupR, tPupR, 0.3f);
  approach(face.conv, tConv, 0.4f);
  face.jitter = jit;

  float openEff = face.open;
  if (face.blinking) {
    float u = (float)(now - face.blinkStart) / 180.0f;
    if (u >= 1.0f) face.blinking = false;
    else openEff = face.open * (1.0f - (u < 0.5f ? u * 2.0f : (1.0f - u) * 2.0f));
  }
  face.openEff = openEff;
}

static void faceDraw(uint32_t now) {
  if (!oledOK) return;
  display.clearDisplay();

  int jx = (int)roundf(face.jitter);
  int ox = (int)roundf(face.offX) + jx;
  int oy = (int)roundf(face.offY);
  int open = (int)roundf(face.openEff);
  int Lx = LEFT_EYE_X + ox, Rx = RIGHT_EYE_X + ox, Ey = EYE_Y + oy;
  int px = (int)roundf(face.pupX), py = (int)roundf(face.pupY);
  int pr = (int)roundf(face.pupR), cv = (int)roundf(face.conv);

  switch (cur) {
    case SLEEPING:
      drawSleeping(now);
      break;
    case HAPPY:
      drawHappyEye(Lx, Ey);
      drawHappyEye(Rx, Ey);
      drawBrows(HAPPY, oy, 0);
      break;
    case LOVE: {
      float s = 1.0f + 0.12f * sinf((now - emoStart) * 0.012f);
      drawHeart(Lx, Ey, s);
      drawHeart(Rx, Ey, s);
      drawBrows(LOVE, oy, 0);
      break;
    }
    default:
      drawEye(Lx, Ey, open, px + cv, py, pr, true);   // pupils converge when angry
      drawEye(Rx, Ey, open, px - cv, py, pr, true);
      drawBrows(cur, oy, jx);
      break;
  }

  if (millis() < muteShowUntil) {
    display.setTextSize(1);
    display.setTextColor(SH110X_WHITE);
    display.setCursor(38, 56);
    display.print(gMuted ? "sound off" : "sound on");
  }
  display.display();
}

// ============================================================
//  BRAIN  (decides how Toki feels)
// ============================================================

static uint32_t lastActivity = 0, nextIdleEvent = 0, lastVoiceReact = 0;
static bool     shakeActive = false, shakeDriven = false;
static uint32_t shakeStart = 0, lastShake = 0;

static void scheduleIdle(uint32_t now) { nextIdleEvent = now + random(IDLE_EVENT_MIN_MS, IDLE_EVENT_MAX_MS); }

static void setEmotion(Emotion e, uint32_t durMs, SoundId snd) {
  uint32_t now = millis();
  cur = e;
  emoStart = now;
  emoUntil = durMs ? now + durMs : 0;
  emoFlag = false;
  shakeDriven = false;
  if (snd != SND_NONE) playSound(snd);
}

// change emotion only if the new one is at least as important as the current one
static void react(Emotion e, uint32_t durMs, SoundId snd) {
  if (EMO_PRIO[e] >= EMO_PRIO[cur]) setEmotion(e, durMs, snd);
}

static void brainUpdate(uint32_t now) {
  bool jolt = takeFlag(imu.evJolt), fall = takeFlag(imu.evFall), handled = takeFlag(imu.evHandled);
  bool loud = takeFlag(micLoud);
  bool click = takeFlag(btn.evClick), dbl = takeFlag(btn.evDouble), lng = takeFlag(btn.evLong);
  uint32_t voice = micVoiceMs;

  if (booting) {
    if (now - bootStart > BOOT_MS) { booting = false; lastActivity = now; scheduleIdle(now); }
    return;
  }

  if (lng) {                                          // long press: mute / unmute
    gMuted = !gMuted;
    if (gMuted) stopSound(); else playSound(SND_BLIP);
    muteShowUntil = now + 1500;
    saveSettings();
  }

  bool shaking = imu.shaking;
  bool startle = jolt || fall || loud;
  bool gentle  = handled || voice > 600 || click || dbl;
  if (startle || shaking || gentle || lng) lastActivity = now;

  // ---- asleep: wake up on activity ----
  if (cur == SLEEPING || cur == SLEEPY) {
    if (startle || shaking)  setEmotion(SURPRISED, 1300, SND_SURPRISE);
    else if (gentle)         setEmotion(NEUTRAL, 0, SND_WAKE);
    else if (cur == SLEEPY && emoUntil && now >= emoUntil) setEmotion(SLEEPING, 0, SND_NONE);
    return;
  }

  // ---- shaking: angry, then dizzy ----
  if (shaking) {
    if (!shakeActive) { shakeActive = true; shakeStart = now; }
    lastShake = now;
    if (cur != DIZZY) {
      if (now - shakeStart > SHAKE_DIZZY_MS) { setEmotion(DIZZY, 0, SND_DIZZY); shakeDriven = true; }
      else if (cur != ANGRY)                 { setEmotion(ANGRY, 0, SND_ANGRY); shakeDriven = true; }
    }
  } else {
    shakeActive = false;
    if (shakeDriven && cur == ANGRY && now - lastShake > 1200) setEmotion(NEUTRAL, 0, SND_NONE);
    if (shakeDriven && cur == DIZZY && now - lastShake > 2500) setEmotion(NEUTRAL, 0, SND_NONE);
  }

  // ---- other reactions ----
  if (!shaking && !shakeDriven) {
    if (jolt || fall || loud) react(SURPRISED, 1400, SND_SURPRISE);
    if (dbl)        react(LOVE, 4000, SND_LOVE);
    else if (click) react(HAPPY, 2500, SND_HAPPY);
    if (voice > 1200 && now - lastVoiceReact > 15000) {
      react(CURIOUS, 2600, SND_CURIOUS);
      lastVoiceReact = now;
    }
  }

  // ---- emotion timeout ----
  if (emoUntil && now >= emoUntil) setEmotion(NEUTRAL, 0, SND_NONE);

  // ---- calm: fall asleep or do something cute ----
  if (cur == NEUTRAL) {
    if (SLEEP_AFTER_MS && now - lastActivity > SLEEP_AFTER_MS) {
      setEmotion(SLEEPY, SLEEPY_MS, SND_SLEEP);
    } else if (now >= nextIdleEvent) {
      scheduleIdle(now);
      if (random(100) < 65) setEmotion(CURIOUS, 2600, SND_CURIOUS);
      else                  setEmotion(HAPPY, 1600, SND_BLIP);
    }
  }
}

static void applyRotation() {
#if AUTO_ROTATE
  static bool last = false;
  if (oledOK && imu.flipped != last) {
    last = imu.flipped;
    display.setRotation(last ? 2 : 0);
  }
#endif
}

// ============================================================
//  SERIAL COMMANDS (for testing)
// ============================================================

static bool dbgImu = false, dbgMic = false;

static void scanI2C() {
  Serial.println("[i2c] scanning...");
  int found = 0;
  for (uint8_t addr = 1; addr < 127; addr++) {
    Wire.beginTransmission(addr);
    if (Wire.endTransmission() == 0) { Serial.printf("[i2c]   device at 0x%02X\n", addr); found++; }
  }
  if (!found) Serial.println("[i2c]   nothing found - check wiring");
}

static void printHelp() {
  Serial.println("Commands: neutral happy angry curious surprised sleepy dizzy love wake");
  Serial.println("          sound <startup|happy|curious|angry|surprise|sleep|wake|dizzy|love|blip>");
  Serial.println("          vol <0-1>  mute  unmute  status  files  imu  mic  help");
}

static void printStatus() {
  Serial.printf("OLED:%s  IMU:%s  MIC:%s  LittleFS:%s  volume:%.2f  muted:%s\n",
                oledOK ? "ok" : "MISSING", imu.ok ? "ok" : "MISSING", micOK ? "ok" : "MISSING",
                fsOK ? "ok" : "FAILED", (float)gVolume, gMuted ? "yes" : "no");
  for (int i = 0; i < SND_COUNT; i++) {
    bool has = fsOK && LittleFS.exists(SOUNDS[i].file);
    Serial.printf("  %-9s %-18s %s\n", SOUNDS[i].name, SOUNDS[i].file, has ? "found" : "missing -> using chirp");
  }
}

static void listFiles() {
  if (!fsOK) { Serial.println("LittleFS not mounted"); return; }
  File root = LittleFS.open("/");
  File f = root.openNextFile();
  int n = 0;
  while (f) {
    Serial.printf("  %s  (%u bytes)\n", f.name(), (unsigned)f.size());
    f = root.openNextFile();
    n++;
  }
  if (!n) Serial.println("  (empty - upload the data folder)");
}

static void handleCommand(String c) {
  c.trim();
  c.toLowerCase();
  if (!c.length()) return;
  lastActivity = millis();
  booting = false;

  if (c == "help")            printHelp();
  else if (c == "neutral")    setEmotion(NEUTRAL, 0, SND_NONE);
  else if (c == "happy")      setEmotion(HAPPY, 2500, SND_HAPPY);
  else if (c == "angry")      setEmotion(ANGRY, 2000, SND_ANGRY);
  else if (c == "curious")    setEmotion(CURIOUS, 2600, SND_CURIOUS);
  else if (c == "surprised")  setEmotion(SURPRISED, 1400, SND_SURPRISE);
  else if (c == "sleepy" || c == "sleep") setEmotion(SLEEPY, SLEEPY_MS, SND_SLEEP);
  else if (c == "dizzy")      setEmotion(DIZZY, 4000, SND_DIZZY);
  else if (c == "love")       setEmotion(LOVE, 4000, SND_LOVE);
  else if (c == "wake")       setEmotion(NEUTRAL, 0, SND_WAKE);
  else if (c == "mute")       { gMuted = true; stopSound(); muteShowUntil = millis() + 1500; saveSettings(); }
  else if (c == "unmute")     { gMuted = false; muteShowUntil = millis() + 1500; playSound(SND_BLIP); saveSettings(); }
  else if (c == "status")     printStatus();
  else if (c == "files")      listFiles();
  else if (c == "imu")        { dbgImu = !dbgImu; Serial.println(dbgImu ? "imu stream ON" : "imu stream OFF"); }
  else if (c == "mic")        { dbgMic = !dbgMic; Serial.println(dbgMic ? "mic stream ON" : "mic stream OFF"); }
  else if (c.startsWith("vol")) {
    float v = c.substring(3).toFloat();
    gVolume = clampf(v, 0.0f, 1.0f);
    Serial.printf("volume = %.2f\n", (float)gVolume);
    saveSettings();
    playSound(SND_BLIP);
  } else if (c.startsWith("sound")) {
    String n = c.substring(5);
    n.trim();
    bool found = false;
    for (int i = 0; i < SND_COUNT; i++) if (n == SOUNDS[i].name) { playSound((SoundId)i); found = true; }
    if (!found) Serial.println("unknown sound name");
  } else {
    Serial.println("unknown command - type help");
  }
}

static void serialPoll() {
  static String line;
  while (Serial.available()) {
    char ch = (char)Serial.read();
    if (ch == '\n' || ch == '\r') {
      if (line.length()) { handleCommand(line); line = ""; }
    } else if (line.length() < 48) {
      line += ch;
    }
  }
}

static void debugStream(uint32_t now) {
  static uint32_t last = 0;
  if (now - last < 200) return;
  last = now;
  if (dbgImu && imu.ok)
    Serial.printf("a[g] %5.2f %5.2f %5.2f | up %5.2f | look %5.2f %5.2f | shake %4.2f%s%s\n",
                  imu.a[0], imu.a[1], imu.a[2], imu.up, imu.lookX, imu.lookY, imu.shake,
                  imu.shaking ? " SHAKING" : "", imu.flipped ? " FLIPPED" : "");
  if (dbgMic && micOK)
    Serial.printf("mic rms %7.1f | base %7.1f | ratio %5.2f | voice %lu ms\n",
                  (float)micRms, (float)micBase, (float)micRatio, (unsigned long)micVoiceMs);
}

// ============================================================
//  SETUP / LOOP
// ============================================================

void setup() {
  Serial.begin(115200);
  uint32_t t0 = millis();
  while (!Serial && millis() - t0 < 1500) delay(10);
  Serial.println("\n=== TOKI v1.0 ===");

  pinMode(PIN_BUTTON, INPUT_PULLUP);
  prefs.begin("toki", false);
  gVolume = clampf(prefs.getFloat("vol", DEFAULT_VOLUME), 0.0f, 1.0f);
  gMuted  = prefs.getBool("mute", false);
  randomSeed(esp_random());

  Wire.begin(PIN_SDA, PIN_SCL);
  Wire.setClock(I2C_CLOCK);
  scanI2C();

  oledOK = display.begin(OLED_ADDR, true);
  if (oledOK) {
    display.setRotation(0);
    display.clearDisplay();
    display.display();
    Serial.println("[oled] SH1106 OK");
  } else {
    Serial.printf("[oled] SH1106 not found at 0x%02X - running without a screen\n", OLED_ADDR);
  }

  fsOK = LittleFS.begin(true);
  Serial.println(fsOK ? "[fs] LittleFS OK" : "[fs] LittleFS FAILED - using chirps only");

  soundQ = xQueueCreate(1, sizeof(uint8_t));
  xTaskCreatePinnedToCore(audioTask, "audio", 6144, nullptr, 3, nullptr, 0);

  imuInit();     // keep Toki still for this one second
  micInit();

  bootStart = millis();
  emoStart = bootStart;
  booting = true;
  playSound(SND_STARTUP);

  printStatus();
  printHelp();
}

void loop() {
  uint32_t now = millis();

  serialPoll();
  static uint32_t lastImu = 0;
  if (now - lastImu >= 5) { lastImu = now; imuUpdate(now); }   // 200 Hz is plenty
  buttonUpdate(now);
  brainUpdate(now);
  applyRotation();

  static uint32_t lastFrame = 0;
  if (now - lastFrame >= FRAME_MS) {
    lastFrame = now;
    faceUpdate(now);
    faceDraw(now);
  }

  debugStream(now);
  delay(1);
}
