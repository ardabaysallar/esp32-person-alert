# Hardware

No soldering and no external wiring: the whole project is one board with a camera, a USB cable and Wi-Fi.

```
   +---------------------- Freenove ESP32-S3 WROOM board ----------------------+
   |                                                                           |
   |  [ESP32-S3 module]---[24-pin FPC]---[camera module (OV3660)]              |
   |   dual core 240 MHz     ribbon         lens points outwards               |
   |   8 MB PSRAM, 16 MB flash                                                 |
   |                                                                           |
   |  [CH343 USB-UART]--(USB-C "UART")--------> Mac/PC: upload + serial log    |
   |  [native USB]-----(USB-C "USB")  (not used in this project)               |
   |  LED on GPIO2 = "person seen"                                             |
   +---------------------------------------------------------------------------+
                         |
                         +-- Wi-Fi 2.4 GHz --> router --> internet --> Gmail SMTP --> your inbox
```

## Parts

| Part | Notes |
|---|---|
| Freenove ESP32-S3 WROOM board kit with camera | Sold on Amazon.de as ASIN B0F48DV38M. Facts below come from Freenove's own tutorial and pinout, <https://github.com/Freenove/Freenove_ESP32_S3_WROOM_Board> |
| USB-C data cable | A charge-only cable will not show a serial port |
| 2.4 GHz Wi-Fi | The ESP32 cannot join 5 GHz |
| Power | USB from a computer or a 5 V phone charger |

## The board

- **Module:** ESP32-S3-WROOM-1, dual-core 240 MHz, Wi-Fi and Bluetooth 5 (LE).
- **Memory:** 8 MB PSRAM (OPI). Flash is 8 MB (black PCB, N8R8) or 16 MB (red PCB, N16R8). The board used for this project reports 16 MB flash and 8 MB PSRAM (read with `esptool flash_id`).
- **Camera connector:** 24-pin FPC on the board. The camera used in this project has "OV3660" printed on its ribbon (see the photo in the README). Freenove's tutorial sketches mention an OV2640, so your kit may ship with either; check the label on your camera.
- **Two USB-C ports:**
  - The one next to the **CH343** chip is the programming and serial port. Use this one.
  - The other is the chip's native USB (GPIO19 D+, GPIO20 D-). It is not used here.
- **Onboard LED:** GPIO2. This project lights it while a person is in view. There is also an RGB LED on GPIO48 (not used).
- **Buttons:** BOOT (GPIO0) and RST (EN).

## Pins used by the camera

The camera pins match `CAMERA_MODEL_ESP32S3_EYE` in Espressif's `camera_pins.h`, which Freenove's own sketches also select.

| Function | GPIO |
|---|---|
| XCLK | 15 |
| SIOD (I2C data) / SIOC (I2C clock) | 4 / 5 |
| VSYNC / HREF / PCLK | 6 / 7 / 13 |
| Data Y2..Y9 | 11, 9, 8, 10, 12, 18, 17, 16 |
| PWDN / RESET | not connected (-1) |

Other pins you must not reuse in this project:

| Pins | Why |
|---|---|
| 35, 36, 37 | Used by the OPI PSRAM |
| 0, 3, 45, 46 | Strapping pins that decide how the chip boots |
| 43, 44 | UART0 TX/RX (the serial log and uploads) |
| 19, 20 | Native USB |
| 38, 39, 40 | SD card (not used here, but unavailable if you add one) |

Free GPIOs for your own additions: 1, 14, 21, 41, 42, 47. Always re-check the Freenove pinout image before wiring something new.

## Setting up the hardware

1. **Check authenticity.** Genuine boards have a FREENOVE logo on the silkscreen. Freenove's `About_Fakes.pdf` says fakes can misbehave.
2. **Seat the camera ribbon with the board unplugged.** Open the latch on the 24-pin connector, slide the ribbon in fully, then press the latch down.
3. **Install the USB driver (macOS).**
   - Download the WCH CH34x driver: <https://www.wch-ic.com/downloads/CH34XSER_MAC_ZIP.html>.
   - On macOS 15, also enable it in *System Settings > General > Login Items & Extensions > Driver Extensions*.
   - Plug the board into the CH343 USB-C port. A port like `/dev/cu.wchusbserial*` should appear. Check with `ls /dev/cu.wchusbserial*`.
4. **Flash the firmware** (see the README), then set up Wi-Fi on the board's hotspot.
5. **Place the camera.** Face the area you want to watch, about 1 to 3 metres from where a person would stand, with decent light. Keep it inside your own space.

## Limits of this hardware

- **Resolution is tiny on purpose.** The model sees 96x96 grayscale pixels. A person must fill a good part of the frame.
- **Speed.** About 3 frames per second with plain TensorFlow Lite Micro (about 313 ms per inference in our tests).
- **Heat and power.** Wi-Fi plus camera plus inference runs warm but is normal for this board. Use a stable 5 V supply.
- **Security.** The Wi-Fi and email password live in the board's flash. Anyone with physical access and tools could read them, so keep the board at home and use a throwaway sender account.
