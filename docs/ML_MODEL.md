# The machine-learning model

The board runs a small convolutional neural network (CNN) that answers one question per frame: **is there a person in this image?** It runs on the ESP32 itself with TensorFlow Lite for Microcontrollers (TFLite Micro). No image leaves the device.

```
 camera frame            preprocessing                model                 decision
 96x96 gray    ---->  pixel - 128  (int8)  ---->  CNN, ~300 KB  ---->  2 scores  ---->  person > 0.70 ?
 (uint8 0..255)        range -128..127            conv layers         notperson/person    3 in a row ?
```

## What the model is

| Item | Value | How we know |
|---|---|---|
| Source | Espressif `esp-tflite-micro`, `examples/person_detection` (derived from TensorFlow Lite Micro) | Fetched from the repo (see `NOTICE`) |
| Format | TensorFlow Lite flatbuffer in a C array, `person_detect_model_data.cc` | The file |
| Size | 300,568 bytes of model data | `g_person_detect_model_data_len` |
| Quantisation | int8 (weights and activations) | Upstream README: "experimental int8 quantized version"; we feed `int8` tensors |
| Input | 96 x 96 x 1 (grayscale) | `model_settings.h` |
| Output | 2 classes: index 0 `notperson`, index 1 `person` | `model_settings.cc` |
| Operators | Conv2D, DepthwiseConv2D, AveragePool2D, Reshape, Softmax | The five ops the sketch registers; allocation succeeds with only these |
| Memory | 82,692 bytes of the tensor arena used (136 KB reserved in PSRAM) | Serial log: `Arena used: 82692 bytes` |
| Speed | about 313 ms per frame on the ESP32-S3 | Serial log |

That operator list is typical of a MobileNet-style network (depthwise-separable convolutions), small enough for a microcontroller. The dataset it was trained on, and its published accuracy, were **not** checked for this document. Look in the upstream repo and the TensorFlow Lite Micro person-detection documentation before quoting numbers.

## Step by step, what happens to one frame

1. **Capture.** The camera is configured for `PIXFORMAT_GRAYSCALE` at `FRAMESIZE_96X96`, so the sensor itself delivers a 96x96 frame. No resizing code is needed.
2. **Frame buffer.** The frame sits in PSRAM (`fb_count = 1`, `CAMERA_GRAB_LATEST` so we always see the newest frame).
3. **Preprocessing.** The model expects signed int8. The loop turns each unsigned pixel `0..255` into `-128..127` by flipping the top bit: `(int8_t)(pixel ^ 0x80)`. That equals `pixel - 128`.
4. **Inference.** `interpreter->Invoke()` runs the network.
5. **Dequantise.** The output is two int8 values. They become real-valued scores with the output tensor's quantisation parameters: `(value - zero_point) * scale`. The two scores sum to about 1 (softmax).
6. **Decision.** If the `person` score is above `kThreshold` (0.70), the frame counts as a hit. The onboard LED follows this directly. After `kHitsNeeded` (3) hits in a row, and if the cooldown has passed, the sketch sends one email.

## Tuning

All three knobs are at the top of `PersonAlert/PersonAlert.ino`.

| Knob | Default | Effect |
|---|---|---|
| `kThreshold` | 0.70 | Higher = fewer false alarms, more missed detections |
| `kHitsNeeded` | 3 | Frames in a row required. At about 3 frames per second, 3 hits is roughly one second |
| `kCooldownMs` | 5 minutes | Minimum time between two emails |

## What we measured (one session, not a benchmark)

These are single observations from setting this up at home, not a test of accuracy.

| Situation | `person` score |
|---|---|
| Camera facing a wall or ceiling | about 0.18 to 0.39 |
| A person standing in front of the camera | 0.84 to 0.95 (64 readings in 20 s) |
| A person holding a pen | 0.09 to 0.53 (it never crossed 0.70) |

For a real accuracy figure you would need many labelled images in your own lighting and camera position.

## Limits you should know about

- **It only knows "person" and "not a person".** It cannot say "pen", "cat" or "package".
- **Small and coarse.** 96x96 grayscale is far less than your eyes get. Distant people, people in poor light or partly out of frame can be missed.
- **False alarms are possible.** Hands, coats on hangers, posters and similar shapes can score high. Raise `kThreshold` or `kHitsNeeded` if you see them.
- **Not a face recogniser.** It does not identify anyone.
- **No tracking.** Each frame is judged on its own. The "3 in a row" rule is the only memory.
- **Slow.** About 3 frames per second, because this build uses plain TFLite Micro kernels. Espressif's optimised esp-nn kernels (available with ESP-IDF) should be much faster.

## Next steps for the model

1. **Train your own classes** (for example "pen" or "package") with Edge Impulse or TensorFlow, quantise to int8, and swap the C array and `model_settings`.
2. **Port to ESP-IDF** with esp-nn for speed.
3. **Add a cheap pre-filter**, such as frame differencing for motion, so the network only runs when something changed.
4. **Evaluate properly:** collect labelled frames from your own camera and measure precision and recall at several thresholds.

## Licence

The model and `model_settings` files are Apache 2.0 licensed (see `NOTICE`). The original headers are kept in the files.
