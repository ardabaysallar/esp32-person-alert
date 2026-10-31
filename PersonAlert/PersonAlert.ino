// Person detection + email alert on the Freenove ESP32-S3 camera (TFLite Micro, 96x96 grayscale, int8)
#include <Chirale_TensorFlowLite.h>
#include "esp_camera.h"
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

#define CAMERA_MODEL_ESP32S3_EYE
#include "camera_pins.h"

constexpr int kLedPin = 2;
constexpr int kTensorArenaSize = 136 * 1024;
constexpr float kThreshold = 0.70f;
constexpr int kHitsNeeded = 3;                       // consecutive detections before alerting
constexpr uint32_t kCooldownMs = 5UL * 60UL * 1000UL; // minimum time between emails

SMTPSession smtp;
int hits = 0;
uint32_t lastAlertMs = 0;
bool everAlerted = false;

void sendAlert(float score) {
  struct tm t;
  char when[32] = "unknown time";
  if (getLocalTime(&t, 2000)) strftime(when, sizeof(when), "%Y-%m-%d %H:%M:%S", &t);

  Session_Config config;
  config.server.host_name = "smtp.gmail.com";
  config.server.port = 465;
  config.login.email = SENDER_EMAIL;
  config.login.password = SENDER_APP_PASS;
  config.login.user_domain = "";
  config.time.ntp_server = "pool.ntp.org";
  config.time.gmt_offset = 1;
  config.time.day_light_offset = 0;

  SMTP_Message message;
  message.sender.name = "ESP32 Camera";
  message.sender.email = SENDER_EMAIL;
  message.subject = "Person detected";
  message.addRecipient("Me", RECIPIENT_EMAIL);
  String body = String("Person detected at ") + when + " (score " + String(score, 2) + ").";
  message.text.content = body.c_str();

  if (!smtp.connect(&config)) { Serial.println("SMTP connect failed"); return; }
  if (!MailClient.sendMail(&smtp, &message)) {
    Serial.printf("Email failed: %s\n", smtp.errorReason().c_str());
  } else {
    Serial.println("Email sent");
  }
  smtp.closeSession();
}

tflite::MicroInterpreter* interpreter = nullptr;
TfLiteTensor* input = nullptr;
uint8_t* tensor_arena = nullptr;

bool initCamera() {
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
  config.pixel_format = PIXFORMAT_GRAYSCALE;
  config.frame_size = FRAMESIZE_96X96;
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

void setup() {
  Serial.begin(115200);
  pinMode(kLedPin, OUTPUT);
  delay(1000);

  WiFiManager wm;  // reuses the Wi-Fi saved in flash; opens "ESP32-CAM-Setup" hotspot if none
  wm.setConnectTimeout(20);
  if (!wm.autoConnect("ESP32-CAM-Setup")) ESP.restart();
  configTzTime("CET-1CEST,M3.5.0,M10.5.0/3", "pool.ntp.org");

  if (!initCamera()) return;

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
  Serial.printf("Model ready. Arena used: %u bytes. Input %dx%d\n",
                (unsigned)interpreter->arena_used_bytes(), kNumCols, kNumRows);
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
    sendAlert(person);
    lastAlertMs = millis();
    everAlerted = true;
    hits = 0;
  }
  Serial.printf("person=%.2f  no_person=%.2f  %s  (%u ms)\n", person, notPerson, seen ? "PERSON" : "-", (unsigned)dt);
}
