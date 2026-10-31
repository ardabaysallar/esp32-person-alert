# How it works

A tour of the firmware, in the order it runs. The code is one file, `PersonAlert/PersonAlert.ino`, plus the model and settings files.

```
power on
  |
  v
setup()  --> Serial 115200, LED pin
  |-> WiFiManager.autoConnect("ESP32-CAM-Setup")   saved Wi-Fi? connect : open setup hotspot
  |-> configTzTime(Berlin, pool.ntp.org)           clock for the email timestamp
  |-> initCamera()                                 grayscale 96x96, frame in PSRAM
  |-> allocate 136 KB tensor arena in PSRAM
  |-> load model, register 5 ops, AllocateTensors()
  v
loop()  (about 3 times per second)
  |-> grab frame --> int8 --> Invoke() --> person score
  |-> LED = score > 0.70
  |-> hits = in a row; 3 hits and cooldown over? --> sendAlert() --> Gmail SMTP
```

## Startup (`setup()`)

1. **Serial and LED.** 115200 baud for logs. GPIO2 is the LED.
2. **Wi-Fi with WiFiManager.** If the board already has Wi-Fi credentials in flash (NVS), it connects to that network. Otherwise it starts a hotspot called **ESP32-CAM-Setup**. You join it, pick your network and enter the password on a web page. Credentials are saved in flash, so they are never in the source code. If the connection fails, the board restarts.
3. **Clock.** `configTzTime("CET-1CEST,...", "pool.ntp.org")` sets Berlin time through NTP, so the email says when the person was seen. Change the time-zone string if you live elsewhere.
4. **Camera.** `initCamera()` fills `camera_config_t` with the pins from `camera_pins.h` and asks for 96x96 grayscale. It also flips the image vertically, because the sensor sits upside down on this board.
5. **Model.** The tensor arena (136 KB) is allocated in PSRAM with `heap_caps_aligned_alloc(..., MALLOC_CAP_SPIRAM)`. The model is loaded from the C array, five operators are registered, and `AllocateTensors()` plans the memory. If PSRAM is not enabled in the Tools menu, this step fails and the log says so.

## The main loop (`loop()`)

1. Grab the newest frame, check it is large enough and convert it to int8.
2. Run the network and read two scores.
3. Switch the LED on if `person > 0.70`.
4. Count consecutive hits. When there are 3, and at least 5 minutes have passed since the last email, call `sendAlert()` and reset the counter.
5. Print one log line: `person=0.93  no_person=0.07  PERSON  (313 ms)`.

## Sending the email (`sendAlert()`)

- **Server:** `smtp.gmail.com` on port 465 (SSL), using the ESP Mail Client library.
- **Login:** the sender address and the 16-character Gmail **app password** from `secrets.h`.
- **Message:** subject "Person detected", text "Person detected at <time> (score 0.93)", sent to the recipient from `secrets.h`.
- **Blocking:** sending takes a couple of seconds, during which the loop pauses. That is fine for one email per 5 minutes.
- **Failures** show up in the log as `SMTP connect failed` or `Email failed: <reason>`.

## Files

| File | Role |
|---|---|
| `PersonAlert/PersonAlert.ino` | Everything above |
| `PersonAlert/secrets.h` (yours, not in git) | Sender, app password, recipient. Created from `secrets.h.example` by `setup.sh` |
| `PersonAlert/camera_pins.h` | Pin maps for many camera boards; `CAMERA_MODEL_ESP32S3_EYE` is selected |
| `PersonAlert/model_settings.*` | 96x96x1 input, class names |
| `PersonAlert/person_detect_model_data.*` | The model as a C array |
| `setup.sh` | Installs `arduino-cli`, ESP32 core 2.0.5, three pinned libraries, compiles, uploads |

## Build settings (and why)

| Setting | Value | Reason |
|---|---|---|
| Board | ESP32S3 Dev Module | Matches the ESP32-S3-WROOM module |
| Core | esp32 **2.0.5** | The version Freenove's tutorial uses. 3.x was not tested here |
| Flash size | 16 MB | This board reports 16 MB (red PCB). Use 8 MB on the black PCB |
| Flash mode | QIO | The board's flash is quad; the IDE may preselect OPI by default |
| PSRAM | OPI | The board's 8 MB PSRAM is octal. Without it the model cannot allocate |
| Partition scheme | 16M Flash (3MB APP/9.9MB FATFS) | Plenty of room: the sketch uses about 47% of the 3 MB app slot |
| Upload mode | UART0 / Hardware CDC | Uploads through the CH343 USB-UART port |

## Resource use (measured when compiling)

| Resource | Used |
|---|---|
| Program storage | 1,503,729 bytes (47% of 3 MB) |
| Global variables | 46,272 bytes (14% of 320 KB) |
| Tensor arena | 82,692 bytes used of 136 KB reserved (in PSRAM) |

## Security design

- Secrets live only in `secrets.h` (git-ignored) and in the board's flash after upload.
- Wi-Fi credentials are entered on the setup hotspot, not in code.
- The sender is a throwaway Gmail account, so a leaked password has limited reach.
- Nothing is stored or streamed: frames are analysed in RAM and overwritten.
- The setup hotspot is open until Wi-Fi is configured, so do first-time setup at home.

## Ideas to extend it

- Attach a snapshot to the email (switch the sensor to JPEG for one frame, then back).
- Add a second alert channel (Telegram or push notification).
- Add a motion pre-filter to save power.
- Train your own model (see `ML_MODEL.md`).
