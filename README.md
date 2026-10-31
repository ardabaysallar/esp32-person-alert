# esp32-person-alert

A small security camera on an ESP32-S3. It runs a person-detection neural network **on the board itself** and emails you when it sees someone. No cloud service, no video leaves the device.

```
 camera (96x96 gray) ──► TFLite Micro model ──► person score
                                                    │  3 hits in a row?
                                                    ▼
                                      email via Gmail SMTP (5 min cooldown)
```

## Documentation

| Document | What's in it |
|---|---|
| [docs/HARDWARE.md](docs/HARDWARE.md) | Parts, board layout, pins, camera and USB setup, limits |
| [docs/ML_MODEL.md](docs/ML_MODEL.md) | The person-detection model, preprocessing, tuning, measured scores, limits |
| [docs/HOW_IT_WORKS.md](docs/HOW_IT_WORKS.md) | Firmware walkthrough, email sending, build settings, resource use, security design |

## At a glance

- Board: Freenove ESP32-S3 WROOM with camera (8 MB PSRAM, 8 or 16 MB flash depending on the variant; the camera is an OV2640 per Freenove's tutorial)
- Model: Espressif / TensorFlow "person detection" (int8, 96x96 grayscale, about 300 KB)
- Speed: about 3 frames per second (plain TFLite Micro, no esp-nn acceleration)
- The onboard LED (GPIO2) lights up while a person is in view

## What you need

- The board: Freenove ESP32-S3 WROOM kit with camera, plus a USB-C **data** cable. See [docs/HARDWARE.md](docs/HARDWARE.md) for how to seat the camera ribbon.
- A computer. Tested on macOS 15 only. The script targets macOS; Linux may work, Windows is untested (use the Arduino IDE route below).
- A 2.4 GHz Wi-Fi network (the ESP32 cannot join 5 GHz).
- Two Gmail addresses: a **throwaway sender** account for the camera, and the address that should receive alerts. The sender needs 2-Step Verification turned on so you can create an app password (about 5 minutes, see "Secrets and security").
- Some patience on the first run: `setup.sh` downloads the ESP32 toolchain, which is a large download.

Tested with: Freenove ESP32-S3 N16R8 (16 MB flash), macOS 15.6, arduino-cli 1.5.1, esp32 core 2.0.5.

## Quick start (macOS)

1. **Driver.** Install the WCH CH34x driver from <https://www.wch-ic.com/downloads/CH34XSER_MAC_ZIP.html>. On macOS 15 also enable it in *System Settings > General > Login Items & Extensions*. Plug in the USB-C port next to the CH343 chip, with a data cable.
2. **Get the code.**
   ```bash
   git clone https://github.com/ardabaysallar/esp32-person-alert.git
   cd esp32-person-alert
   ```
3. **Run setup.** It installs `arduino-cli`, the ESP32 core 2.0.5 and the pinned libraries, then creates `PersonAlert/secrets.h` for you:
   ```bash
   ./setup.sh
   ```
4. **Fill in your secrets** in `PersonAlert/secrets.h` (see below), save, then run `./setup.sh` again. It compiles and uploads.
5. **Wi-Fi.** If the board has no saved Wi-Fi, it opens a hotspot called **ESP32-CAM-Setup**. Join it from your phone and enter your 2.4 GHz Wi-Fi name and password on the page that appears (or open `192.168.4.1`). They are stored in the board's flash, not in the code. *Note: the author's board already had Wi-Fi saved, so this hotspot step was not exercised in testing. It is the standard WiFiManager flow; open an issue if it behaves differently.*
6. **Check that it works.** Open the Arduino IDE's Serial Monitor (Tools > Serial Monitor, 115200 baud, with the board's port selected), or in a terminal run `screen /dev/cu.wchusbserial* 115200` (leave with Ctrl-A, then K, then Y). `setup.sh` also prints an `arduino-cli monitor` command at the end. You should see `Model ready. Arena used: ... bytes` and then a line per frame like `person=0.18  no_person=0.82  -  (313 ms)`. The "person" value is the model's confidence from 0 to 1.
7. **Test the alert.** Stand in front of the camera, 1 to 2 metres away. The onboard LED lights, the score goes above 0.7 (`PERSON` in the log), and after about one second you get an email "Person detected". Look in your spam folder the first time. There is a 5 minute pause between emails.

`./setup.sh compile` only compiles, with no board and no secrets needed.

### 8 MB flash variant (black PCB)

`setup.sh` is set up for the 16 MB variant. For 8 MB, edit the `FQBN=` line in `setup.sh`: replace `FlashSize=16M` with `FlashSize=8M` and `PartitionScheme=app3M_fat9M_16MB` with `PartitionScheme=default_8MB`. This variant was not tested.

### Arduino IDE instead of the script

Install the ESP32 core **2.0.5** (not 3.x) and the libraries `WiFiManager` 2.0.17, `ESP Mail Client` 3.4.24 and `Chirale_TensorFLowLite` 2.0.0. Copy `PersonAlert/secrets.h.example` to `PersonAlert/secrets.h`, fill it in, open `PersonAlert/PersonAlert.ino` and choose:

| Tools menu | Value |
|---|---|
| Board | ESP32S3 Dev Module |
| Flash Size | 16MB |
| PSRAM | OPI PSRAM |
| Flash Mode | QIO 80MHz |
| Partition Scheme | 16M Flash (3MB APP/9.9MB FATFS) |
| Upload Mode | UART0 / Hardware CDC |
| USB CDC On Boot | Disabled |

The tools only appear in the macOS menu bar after you select a board. If your kit is the 8 MB flash variant (black PCB), use 8MB and a matching partition scheme.

## Secrets and security

Everything private lives in one file, `PersonAlert/secrets.h`, which is listed in `.gitignore` and never committed.

**Create the app password** (once, on the sender account): turn on 2-Step Verification in your Google account, then open <https://myaccount.google.com/apppasswords>, create a password named e.g. "esp32 camera" and copy the 16 characters Google shows. Google shows it only once. If that page is missing, 2-Step Verification is not on yet.

| Value | What to use |
|---|---|
| `SENDER_EMAIL` | A **separate throwaway Gmail account** for the camera, not your main account |
| `SENDER_APP_PASS` | A 16-character Gmail **app password** (needs 2-Step Verification). Never your normal password |
| `RECIPIENT_EMAIL` | Where alerts should arrive |

Good practice:

- The password is compiled into the board's firmware. Anyone with physical access and tools could read it back, so use the throwaway account and keep the board at home.
- Do not post `secrets.h`, screenshots of it, or serial logs that contain it. If it leaks, revoke the app password in your Google account and make a new one.
- Wi-Fi credentials are entered through the setup hotspot and are not in the source.
- Before you push to a fork, run `git status` and check that `secrets.h` is not listed.
- Run it on your own network. The device has no login and no encryption on its hotspot during setup, so do the setup at home.

## How it works

| File | Purpose |
|---|---|
| `PersonAlert/PersonAlert.ino` | Camera setup, inference loop, alert logic, email sending |
| `PersonAlert/person_detect_model_data.*` | The int8 TFLite model as a C array |
| `PersonAlert/model_settings.*` | Input size (96x96x1) and class labels |
| `PersonAlert/camera_pins.h` | Camera pin maps; this project uses `CAMERA_MODEL_ESP32S3_EYE`, which matches the Freenove board |
| `PersonAlert/secrets.h.example` | Template for your private settings |
| `setup.sh` | One-command install, compile and upload |

Tunables at the top of `PersonAlert.ino`: `kThreshold` (0.70), `kHitsNeeded` (3 frames in a row) and `kCooldownMs` (5 minutes between emails). The time zone is set for Berlin (`CET-1CEST`); change it in `setup()`.

## Troubleshooting

- **No serial port.** `ls /dev/cu.wchusbserial*`. If empty: driver, data cable, or the other USB-C port.
- **Uploads fail.** Check the port and cable first. Then press RST or re-plug the board and try again (Freenove's own advice). If it still fails, hold BOOT while you press RST, release BOOT, then upload.
- **"PSRAM" or allocation errors.** PSRAM must be set to OPI PSRAM.
- **Log shows nothing.** Make sure the baud rate is 115200 and that you use the UART USB-C port. Press RST.
- **No email.** Check the serial monitor (115200 baud) for `Email failed: ...`. Usual causes are a wrong app password or 2-Step Verification being off. Look in spam too.
- **False alarms.** Hands, coats and posters can score high. Raise `kThreshold` or `kHitsNeeded`.

## Privacy and legal

This device detects people. Use it only in your own space, and in Germany or the EU keep it away from neighbours, shared hallways, the street and visitors who don't know about it (GDPR/BDSG and criminal law on picture-taking apply). Nothing is recorded or stored: frames are analysed in memory and discarded. This is not legal advice.

## Credits and licence

- Person-detection model and the `model_settings` files: [espressif/esp-tflite-micro](https://github.com/espressif/esp-tflite-micro) (`examples/person_detection`), derived from TensorFlow Lite Micro, Apache License 2.0. See `NOTICE`.
- `camera_pins.h` comes from Espressif's Arduino-ESP32 CameraWebServer example.
- Libraries: WiFiManager (tzapu), ESP Mail Client (Mobizt), Chirale TensorFlowLite.
- The code in this repository is MIT licensed (see `LICENSE`). Third-party files keep their own licences.
