// Person detection on the Freenove ESP32-S3 camera (TFLite Micro, 96x96 grayscale, int8).
// Emails an alert through Gmail when a person is seen. Optional: attach ONE photo (off by default).
//
// Privacy design of the optional photo: it exists only in RAM (PSRAM) from capture until the email
// is sent, then the frame buffer is released. Nothing is written to flash, SD or NVS. No video, no
// stream, no web server. READ the "Photo mode: risks" section of the README before enabling it.
#include <Chirale_TensorFlowLite.h>
#include "esp_camera.h"
#include "esp_log.h"
#include <WiFi.h>
#include <WiFiManager.h>
#include <ESP_Mail_Client.h>
#include <time.h>
#include "secrets.h"
#include "tensorflow/lite/micro/micro_interpreter.h"
#include "tensorflow/lite/micro/micro_mutable_op_resolver.h"
#include "tensorflow/lite/schema/schema_generated.h"
#include "model_settings.h"
#include "person_detect_model_data.h"

// ---- Settings ------------------------------------------------------------------------
// Personal overrides go in PersonAlert/config_local.h (git-ignored, see config_local.h.example).
#if __has_include("config_local.h")
#include "config_local.h"
#endif
#ifndef ENABLE_PHOTO
#define ENABLE_PHOTO 0   // 0 = text-only email (default). 1 = attach one snapshot (see README risks)
#endif
#ifndef COOLDOWN_MINUTES_X10
#define COOLDOWN_MINUTES_X10 50  // 50 = 5.0 minutes between emails. config_local.h may lower it for tests
#endif
// ----------------------------------------------------------------------------------------

#define CAMERA_MODEL_ESP32S3_EYE
#include "camera_pins.h"

constexpr int kLedPin = 2;
constexpr int kTensorArenaSize = 136 * 1024;
constexpr float kThreshold = 0.70f;
constexpr int kHitsNeeded = 3;                        // consecutive detections before alerting
constexpr uint32_t kCooldownMs = (uint32_t)COOLDOWN_MINUTES_X10 * 6000UL;  // minimum time between emails (and photos)
constexpr uint32_t kRetryMs = 30UL * 1000UL;           // after a FAILED send, try again after this long
constexpr int kWarmupFrames = 3;                      // frames discarded after switching to JPEG (auto exposure)

SMTPSession smtp;
int hits = 0;
uint32_t lastAlertMs = 0;
bool everAlerted = false;

tflite::MicroInterpreter* interpreter = nullptr;
TfLiteTensor* input = nullptr;
uint8_t* tensor_arena = nullptr;

// ---- Camera ----
// jpeg=false: 96x96 grayscale for the model. jpeg=true: VGA JPEG for the single alert photo.
bool initCamera(bool jpeg) {
  camera_config_t config = {};
  config.ledc_channel = LEDC_CHANNEL_0;
  config.ledc_timer = LEDC_TIMER_0;
  config.pin_d0 = Y2_GPIO_NUM;
  config.pin_d1 = Y3_GPIO_NUM;
  config.pin_d2 = Y4_GPIO_NUM;
  config.pin_d3 = Y5_GPIO_NUM;
  config.pin_d4 = Y6_GPIO_NUM;
  config.pin_d5 = Y7_GPIO_NUM;
  config.pin_d6 = Y8_GPIO_NUM;
  config.pin_d7 = Y9_GPIO_NUM;
  config.pin_xclk = XCLK_GPIO_NUM;
  config.pin_pclk = PCLK_GPIO_NUM;
  config.pin_vsync = VSYNC_GPIO_NUM;
  config.pin_href = HREF_GPIO_NUM;
  config.pin_sccb_sda = SIOD_GPIO_NUM;
  config.pin_sccb_scl = SIOC_GPIO_NUM;
  config.pin_pwdn = PWDN_GPIO_NUM;
  config.pin_reset = RESET_GPIO_NUM;
  config.xclk_freq_hz = 20000000;
  config.pixel_format = jpeg ? PIXFORMAT_JPEG : PIXFORMAT_GRAYSCALE;
  config.frame_size = jpeg ? FRAMESIZE_VGA : FRAMESIZE_96X96;
  config.jpeg_quality = 12;  // 0-63, lower = better quality (only used for JPEG)
  config.fb_location = CAMERA_FB_IN_PSRAM;
  config.fb_count = 1;
  config.grab_mode = CAMERA_GRAB_LATEST;
  esp_err_t err = esp_camera_init(&config);
  if (err != ESP_OK) {
    Serial.printf("Camera init failed: 0x%x\n", err);
    return false;
  }
  sensor_t* s = esp_camera_sensor_get();
  s->set_vflip(s, 1);  // Freenove board mounts the sensor upside down
  return true;
}

static void logMem(const char* tag) {
  Serial.printf("[mem] %s: heap %u, psram %u\n", tag, (unsigned)ESP.getFreeHeap(), (unsigned)ESP.getFreePsram());
}

// One mail attempt. photo may be null (text only). Objects are static so they do not sit on the 8 KB loop stack.
// Never print credentials: only status and error reasons are logged.
static bool sendMailOnce(float score, const char* when, camera_fb_t* photo) {
  static Session_Config config;
  static SMTP_Message message;
  static SMTP_Attachment att;
  message.clear();  // reset leftovers from a previous attempt
  att = SMTP_Attachment();

  config.server.host_name = "smtp.gmail.com";
  config.server.port = 465;
  config.login.email = SENDER_EMAIL;
  config.login.password = SENDER_APP_PASS;
  config.login.user_domain = "";
  config.time.ntp_server = "pool.ntp.org";
  config.time.gmt_offset = 1;
  config.time.day_light_offset = 0;

  message.sender.name = "ESP32 Camera";
  message.sender.email = SENDER_EMAIL;
  message.subject = "Person detected";
  message.addRecipient("Me", RECIPIENT_EMAIL);
  static String body;
  body = String("Person detected at ") + when + " (score " + String(score, 2) + ")." +
         (photo ? " One photo attached." : "");
  message.text.content = body.c_str();

  // Attachment fields as in the ESP Mail Client examples (Send_Camera_Image, Send_Attachment_Blob).
  // The library reads blob.data while sending, so `photo` must stay valid until sendMail returns.
  if (photo) {
    att.descr.filename = F("snapshot.jpg");
    att.descr.mime = F("image/jpeg");
    att.blob.data = photo->buf;
    att.blob.size = photo->len;
    att.descr.transfer_encoding = Content_Transfer_Encoding::enc_base64;
    message.addAttachment(att);
  }

  logMem(photo ? "before SMTP (photo)" : "before SMTP (text)");
  if (!smtp.connect(&config)) {
    Serial.printf("SMTP connect failed: code %d, %s\n", smtp.statusCode(), smtp.errorReason().c_str());
    smtp.closeSession();
    return false;
  }
  bool ok = MailClient.sendMail(&smtp, &message);
  if (!ok) Serial.printf("Email failed: code %d, %s\n", smtp.statusCode(), smtp.errorReason().c_str());
  smtp.closeSession();
  logMem("after SMTP");
  return ok;
}

// Returns true when some mail (with or without photo) was sent.
bool sendAlert(float score) {
  struct tm t;
  char when[32] = "unknown time";
  if (getLocalTime(&t, 2000)) strftime(when, sizeof(when), "%Y-%m-%d %H:%M:%S", &t);
  logMem("alert start");

  camera_fb_t* photo = nullptr;
  bool switchedMode = false;
#if ENABLE_PHOTO
  // Switch the sensor from 96x96 gray to VGA JPEG, let exposure settle, keep exactly one frame in RAM.
  esp_camera_deinit();
  switchedMode = true;
  if (initCamera(true)) {
    for (int i = 0; i < kWarmupFrames; i++) {
      camera_fb_t* f = esp_camera_fb_get();
      if (f) esp_camera_fb_return(f);
    }
    photo = esp_camera_fb_get();
    if (photo) Serial.printf("Photo: %u bytes\n", (unsigned)photo->len);
    else Serial.println("Photo capture failed, sending text only");
  } else {
    Serial.println("JPEG init failed, sending text only");
  }
  logMem("after capture");
#endif

  bool sent = sendMailOnce(score, when, photo);
  if (sent) {
    Serial.println(photo ? "Email sent (with photo)" : "Email sent (text only)");
  } else if (photo) {
    Serial.println("Mail with photo failed, retrying text only");
    sent = sendMailOnce(score, when, nullptr);
    Serial.println(sent ? "Email sent (text only, fallback)" : "Text-only fallback failed too");
  }

  // Release the photo right away; it is never copied or stored anywhere.
  if (photo) esp_camera_fb_return(photo);
  photo = nullptr;
  if (switchedMode) {
    esp_camera_deinit();
    if (!initCamera(false)) {  // cannot get back to model mode: restart is the safe recovery
      Serial.println("Could not restore 96x96 mode, restarting");
      ESP.restart();
    }
    logMem("alert end");
  }
  return sent;
}

void setup() {
  Serial.begin(115200);
  // The camera driver's cam_task has a tiny stack. When it logs a warning (e.g. dropped frame) the printf
  // overflows it ("Stack canary watchpoint triggered (cam_task)") and the board reboots. Silence its logs.
  esp_log_level_set("cam_hal", ESP_LOG_NONE);
  esp_log_level_set("camera", ESP_LOG_NONE);
  pinMode(kLedPin, OUTPUT);
  delay(1000);

  WiFiManager wm;  // reuses the Wi-Fi saved in flash; opens "ESP32-CAM-Setup" hotspot if none
  wm.setConnectTimeout(20);
  if (!wm.autoConnect("ESP32-CAM-Setup")) ESP.restart();
  configTzTime("CET-1CEST,M3.5.0,M10.5.0/3", "pool.ntp.org");

  if (!initCamera(false)) return;

  tensor_arena = (uint8_t*)heap_caps_aligned_alloc(16, kTensorArenaSize, MALLOC_CAP_SPIRAM);
  if (!tensor_arena) {
    Serial.println("Could not allocate tensor arena in PSRAM (is PSRAM set to OPI?)");
    return;
  }

  const tflite::Model* model = tflite::GetModel(g_person_detect_model_data);
  static tflite::MicroMutableOpResolver<5> resolver;
  resolver.AddAveragePool2D();
  resolver.AddConv2D();
  resolver.AddDepthwiseConv2D();
  resolver.AddReshape();
  resolver.AddSoftmax();

  static tflite::MicroInterpreter static_interpreter(model, resolver, tensor_arena, kTensorArenaSize);
  interpreter = &static_interpreter;
  if (interpreter->AllocateTensors() != kTfLiteOk) {
    Serial.println("AllocateTensors failed");
    interpreter = nullptr;
    return;
  }
  input = interpreter->input(0);
  Serial.printf("Model ready. Arena used: %u bytes. Input %dx%d. Photo %s\n",
                (unsigned)interpreter->arena_used_bytes(), kNumCols, kNumRows, ENABLE_PHOTO ? "ON" : "OFF");
}

void loop() {
  if (!interpreter) { delay(1000); return; }

  camera_fb_t* fb = esp_camera_fb_get();
  if (!fb) { Serial.println("Frame grab failed"); return; }
  if (fb->len < (size_t)kMaxImageSize) {
    Serial.printf("Unexpected frame size %u\n", (unsigned)fb->len);
    esp_camera_fb_return(fb);
    return;
  }
  // Model expects int8: shift unsigned pixels 0..255 to -128..127
  for (int i = 0; i < kMaxImageSize; i++) input->data.int8[i] = (int8_t)(fb->buf[i] ^ 0x80);
  esp_camera_fb_return(fb);

  uint32_t t0 = millis();
  if (interpreter->Invoke() != kTfLiteOk) { Serial.println("Invoke failed"); return; }
  uint32_t dt = millis() - t0;

  TfLiteTensor* output = interpreter->output(0);
  float person = (output->data.int8[kPersonIndex] - output->params.zero_point) * output->params.scale;
  float notPerson = (output->data.int8[kNotAPersonIndex] - output->params.zero_point) * output->params.scale;
  bool seen = person > kThreshold;
  digitalWrite(kLedPin, seen ? HIGH : LOW);
  hits = seen ? hits + 1 : 0;


  bool cooledDown = !everAlerted || (millis() - lastAlertMs > kCooldownMs);
  if (hits >= kHitsNeeded && cooledDown) {
    bool sent = sendAlert(person);
    // A failed send must not lock the alert for the whole cooldown: retry after kRetryMs.
    lastAlertMs = sent ? millis() : millis() - kCooldownMs + kRetryMs;
    everAlerted = true;
    hits = 0;
  }
  Serial.printf("person=%.2f  no_person=%.2f  %s  hits=%d  (%u ms)\n", person, notPerson, seen ? "PERSON" : "-", hits, (unsigned)dt);
}
