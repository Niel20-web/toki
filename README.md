# Toki v1.0

A tiny robot with feelings, on a Seeed XIAO ESP32-S3. Toki has animated eyes on an OLED, reacts to being shaken, tilted, bumped, petted and talked to, and answers with little chirps.

## Files

| File | What it is |
|---|---|
| `toki.ino` | The sketch |
| `toki_types.h` | Shared types (must sit in the same folder as `toki.ino`) |
| `data/` | Sound files, uploaded to the board's LittleFS (the sketch uses 10 of them; `bluetooth`, `cheerful`, `chuckle`, `low_battery` and `pomodoro_finish` are not used yet) |
| `tools/make_sounds.py` | Regenerates the sounds (optional) |

## Wiring

| Part | Pin on part | XIAO ESP32-S3 |
|---|---|---|
| OLED SH1106 (I2C, 0x3C) | VCC / GND | 3V3 / GND |
| | SDA / SCL | D4 (GPIO5) / D5 (GPIO6) |
| MPU6050 (I2C, 0x69) | VCC / GND | 3V3 / GND |
| | SDA / SCL | D4 (GPIO5) / D5 (GPIO6) |
| | AD0 | 3V3 |
| INMP441 mic | VDD / GND | 3V3 / GND |
| | SCK | D0 (GPIO1) |
| | WS | D1 (GPIO2) |
| | SD | D2 (GPIO3) |
| | L/R | GND |
| MAX98357 amp | VIN / GND | 5V (or 3V3) / GND |
| | BCLK | D9 (GPIO8) |
| | LRC | D8 (GPIO7) |
| | DIN | D10 (GPIO9) |
| Button | | the onboard BOOT button (GPIO0) |

Speaker goes on the amp's output terminals.

## Setup

1. **Arduino IDE**: install the *esp32 by Espressif* board package (version 3.x), then pick the board **XIAO_ESP32S3**.
2. **Libraries** (Library Manager): *Adafruit GFX Library* and *Adafruit SH110X*. Audio and mic use the built-in `ESP_I2S`, so the old ESP8266Audio library is **not** needed.
3. **Tools menu**: set **USB CDC On Boot: Enabled**, and **Partition Scheme** to one that includes a data partition (for example *8M with spiffs*, or the default scheme if it lists SPIFFS). The `data` folder is about 0.9 MB, so a scheme with at least 1.5 MB of SPIFFS/LittleFS is needed.
4. **Upload the sounds**: use a LittleFS upload plugin for Arduino IDE 2.x (for example *arduino-littlefs-upload*), with the `data` folder next to `toki.ino`. Then upload the sketch.

If a sound file is missing, Toki plays a built-in chirp instead, so he always makes noise. Type `status` in the Serial Monitor to see which files were found.

## What Toki does

| You do this | Toki does this |
|---|---|
| Leave him alone | Blinks, glances around, now and then gets curious or happy |
| Idle for 60 s | Gets sleepy, then falls asleep (Zzz) |
| Tilt him | His eyes look the way he is leaning |
| Turn him upside down | The screen flips to stay readable |
| Click the BOOT button | Happy |
| Double-click | Love (hearts) |
| Hold for 1.5 s | Mute / unmute (remembered after reboot) |
| Shake him | Angry; keep shaking and he gets dizzy |
| Bump or drop him | Surprised |
| Clap or make a loud noise | Surprised |
| Talk near him | Curious |
| Touch, move or talk to him while asleep | Wakes up (a bump or shake wakes him startled) |

The mic ignores Toki's own speaker, and adapts to the room's background noise during the first 1.5 seconds after power-up, so keep things quiet then. Keep him still for the first second too, because that is when the gyro is zeroed.

## Serial commands (115200 baud)

`help`, `status`, `files`, `imu`, `mic` (toggle live sensor streams), `vol 0.0-1.0`, `mute`, `unmute`, the emotions `neutral happy angry curious surprised sleepy dizzy love wake`, and `sound <name>` (`startup happy curious angry surprise sleep wake dizzy love blip`).

## Tuning

All the knobs are at the top of `toki.ino`:

- **Eyes look the wrong way when tilting**: flip `LOOK_X_SIGN` / `LOOK_Y_SIGN`.
- **Screen flips at the wrong time, or never**: run `imu`, hold Toki the way you want "upright", and set `UP_AXIS` (0=X, 1=Y, 2=Z) and `UP_SIGN` so that axis reads +1 g.
- **Reacts to quiet sounds, or ignores claps**: run `mic`, then adjust `MIC_LOUD_RATIO`, `MIC_LOUD_MIN`, `MIC_VOICE_RATIO`, `MIC_VOICE_MIN`.
- **Too sensitive or not sensitive to shaking**: `SHAKE_ON`, `SHAKE_OFF`, `JOLT_G`.
- **Falls asleep too fast / never**: `SLEEP_AFTER_MS` (0 = never).

## Troubleshooting

- **Nothing on the OLED**: the boot log prints an I2C scan. You should see 0x3C and 0x69. If it lists nothing, check power and that header pins are soldered.
- **No sound, but the log says audio is fine**: check the amp's BCLK/LRC/DIN wiring and that the speaker is on the amp's output terminals.
- **"LittleFS FAILED"**: the partition scheme has no data partition. Change it in the Tools menu and upload again.
- **Mic reactions disabled**: the log says `[mic] INMP441 failed to start`. Check SCK/WS/SD and that L/R is on GND.
