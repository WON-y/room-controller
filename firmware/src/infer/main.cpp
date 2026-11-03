// [2단계] 추론 + 제어 펌웨어
//  10 Hz 로 센서 샘플링 → 3초 슬라이딩 윈도 → 1초마다 분류 → 다수결 → 서보 자동 각도
//  조이스틱으로 현장 수동 조작 (버튼 = 자동 복귀), 대시보드 수동 조작도 지원
//  1초마다 comm 컨테이너에 telemetry POST (상태 변경/1분 주기면 report → DB 저장)
//
// Edge Impulse 라이브러리(lib/ 아래, 헤더 room_inferencing.h)가 있으면 그 모델을 사용,
// 없으면 규칙 기반 분류기로 대체 (CI 에서 모델 없이도 빌드되도록)
#include <Arduino.h>
#include <ArduinoJson.h>
#include <HTTPClient.h>
#include <WiFi.h>

#include "RoomLogic.h"
#include "sensors.h"

#if __has_include(<room_inferencing.h>)
#include <room_inferencing.h>
#define HAS_EI 1
#define WINDOW_SAMPLES EI_CLASSIFIER_RAW_SAMPLE_COUNT
static_assert(EI_CLASSIFIER_RAW_SAMPLES_PER_FRAME == 3, "Edge Impulse 축은 temp,hum,sound 3개여야 함");
#else
#define HAS_EI 0
#define WINDOW_SAMPLES 30  // 3 s
#endif

#if __has_include("secrets.h")
#include "secrets.h"
#else
#include "secrets.example.h"
#endif

static const char* MODEL_NAME = HAS_EI ? "edge-impulse" : "rule";

Sensors sensors;
room::Smoother smoother;
room::Reporter reporter(60000);
room::ServoController servo;

Sample window[WINDOW_SAMPLES];
int wIdx = 0, wCount = 0;
uint32_t nextSample = 0, nextInfer = 0, nextServo = 0;
int autoAngle = 0;
int dashboardAngle = -1;  // 대시보드 수동 조작 (-1 = 없음)

struct Result {
  int label = room::CALM;
  float probs[room::N_LABELS] = {1, 0, 0};
  float anomaly = 0;
  room::Features feat{0, 0, 0};
};

static room::Features windowFeatures() {
  room::Features f{0, 0, 0};
  for (int i = 0; i < wCount; ++i) {
    f.temp += window[i].temp;
    f.hum += window[i].hum;
    f.soundRate += window[i].sound;
  }
  const float n = wCount ? wCount : 1;
  f.temp /= n; f.hum /= n; f.soundRate /= n;
  return f;
}

static Result classify() {
  Result r;
  r.feat = windowFeatures();
#if HAS_EI
  static float buf[EI_CLASSIFIER_DSP_INPUT_FRAME_SIZE];
  for (int i = 0; i < WINDOW_SAMPLES; ++i) {  // 링버퍼 → 시간순
    const Sample& s = window[(wIdx + i) % WINDOW_SAMPLES];
    buf[i * 3 + 0] = s.temp;
    buf[i * 3 + 1] = s.hum;
    buf[i * 3 + 2] = s.sound;
  }
  signal_t sig;
  numpy::signal_from_buffer(buf, EI_CLASSIFIER_DSP_INPUT_FRAME_SIZE, &sig);
  ei_impulse_result_t res = {0};
  if (run_classifier(&sig, &res, false) != EI_IMPULSE_OK) return r;
  float best = -1;
  for (size_t i = 0; i < EI_CLASSIFIER_LABEL_COUNT; ++i) {
    const int l = room::labelFromName(res.classification[i].label);
    if (l < 0) continue;
    r.probs[l] = res.classification[i].value;
    if (r.probs[l] > best) { best = r.probs[l]; r.label = l; }
  }
#if EI_CLASSIFIER_HAS_ANOMALY
  r.anomaly = res.anomaly;
#endif
#else
  r.label = room::ruleClassify(r.feat, r.probs);
#endif
  return r;
}

static void buildTelemetry(JsonDocument& doc, const Result& r, int label, const char* report) {
  doc["device_id"] = WiFi.macAddress();
  doc["label"] = room::labelName(label);
  doc["raw_label"] = room::labelName(r.label);
  doc["confidence"] = r.probs[label];
  JsonObject p = doc["probs"].to<JsonObject>();
  for (int i = 0; i < room::N_LABELS; ++i) p[room::labelName(i)] = r.probs[i];
  doc["anomaly"] = r.anomaly;
  doc["temp"] = r.feat.temp;
  doc["hum"] = r.feat.hum;
  doc["sound_rate"] = r.feat.soundRate;
  doc["servo_angle"] = sensors.servoAngle();
  doc["auto_angle"] = autoAngle;
  doc["control"] = room::controlName(servo.control());
  doc["model"] = MODEL_NAME;
  if (report) doc["report"] = report;
  doc["uptime_ms"] = millis();
}

// USB 시리얼로도 같은 telemetry 를 '@' + JSON 한 줄로 출력 → tools/serial_bridge.py 가 대시보드로 전달
// (WiFi 없이 USB 만 연결해도 대시보드/Supabase 동작)
static void serialTelemetry(const JsonDocument& doc) {
  Serial.print('@');
  serializeJson(doc, Serial);
  Serial.println();
}

// 브리지가 대시보드 수동 각도를 시리얼로 돌려줌: "O90\n" / "O-1\n"(해제)
static void readSerialCommands() {
  static char buf[16];
  static int n = 0;
  while (Serial.available()) {
    const char c = Serial.read();
    if (c == '\n' || c == '\r') {
      buf[n] = 0;
      if (n > 1 && buf[0] == 'O') {
        const int a = atoi(buf + 1);
        dashboardAngle = (a >= 0 && a <= 180) ? a : -1;
      }
      n = 0;
    } else if (n < (int)sizeof(buf) - 1) {
      buf[n++] = c;
    }
  }
}

static void postTelemetry(const JsonDocument& doc) {
  if (WiFi.status() != WL_CONNECTED) return;
  String body;
  serializeJson(doc, body);

  HTTPClient http;
  http.setTimeout(500);
  http.begin(String("http://") + COMM_HOST + ":" + COMM_PORT + "/api/telemetry");
  http.addHeader("Content-Type", "application/json");
  if (http.POST(body) == 200) {  // 응답: {"override": null | 각도}
    JsonDocument resp;
    if (!deserializeJson(resp, http.getString())) dashboardAngle = resp["override"] | -1;
  }
  http.end();
}

void setup() {
  Serial.begin(115200);
  sensors.begin();
  sensors.servoWrite(0);
  Serial.printf("\nRoom controller (model=%s, window=%d)\n", MODEL_NAME, WINDOW_SAMPLES);
  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASS);  // 연결 안 돼도 로컬 제어는 계속 동작
}

void loop() {
  const uint32_t now = millis();
  readSerialCommands();

  if ((int32_t)(now - nextSample) >= 0) {
    nextSample = now + SAMPLE_INTERVAL_MS;
    window[wIdx] = sensors.read();
    wIdx = (wIdx + 1) % WINDOW_SAMPLES;
    if (wCount < WINDOW_SAMPLES) ++wCount;
  }

  // 서보는 조이스틱 반응을 위해 20 Hz 로 갱신
  if ((int32_t)(now - nextServo) >= 0) {
    nextServo = now + 50;
    sensors.servoWrite(servo.update(autoAngle, sensors.joyX(), sensors.joyPressed(), dashboardAngle, 0.05f));
  }

  if (wCount == WINDOW_SAMPLES && (int32_t)(now - nextInfer) >= 0) {
    nextInfer = now + 1000;
    const Result r = classify();
    const int label = smoother.push(r.label);
    autoAngle = room::servoAngleFor(label);
    const char* report = reporter.check(label, now);
    Serial.printf("[%s] %s (%.0f%%) T=%.1f H=%.0f snd=%.2f servo=%d(%s) %s\n", MODEL_NAME, room::labelName(label),
                  r.probs[label] * 100, r.feat.temp, r.feat.hum, r.feat.soundRate, sensors.servoAngle(),
                  room::controlName(servo.control()), report ? report : "");
    JsonDocument doc;
    buildTelemetry(doc, r, label, report);
    serialTelemetry(doc);
    postTelemetry(doc);
  }
}
