// 수집용/추론용 펌웨어가 공유하는 센서 읽기 + 서보 + 조이스틱 (Arduino 전용)
#pragma once
#include <Arduino.h>
#include <DHT.h>

#include "pins.h"

struct Sample {
  float temp, hum, sound;  // sound: 직전 샘플 이후 소리 감지 여부 0/1
};

class Sensors {
 public:
  Sensors() : dht_(PIN_DHT, DHT11) {}

  void begin() {
    dht_.begin();
    pinMode(PIN_SOUND, INPUT);
    pinMode(PIN_JOY_SW, INPUT_PULLUP);
    // 사운드 DO 펄스는 매우 짧아서 폴링하면 놓친다 → 인터럽트로 래치
    attachInterrupt(digitalPinToInterrupt(PIN_SOUND), onSound, CHANGE);
    // 서보: LEDC 50 Hz, 14 bit (ESP32-S3 LEDC 최대 14 bit, Arduino core 2.x API)
    ledcSetup(kServoCh, 50, kServoBits);
    ledcAttachPin(PIN_SERVO, kServoCh);
    joyCenter_ = analogRead(PIN_JOY_X);  // 부팅 시 스틱을 건드리지 않은 값 = 중앙
  }

  // SAMPLE_INTERVAL_MS 마다 호출
  Sample read() {
    const uint32_t now = millis();
    if (lastDht_ == 0 || now - lastDht_ >= DHT_INTERVAL_MS) {
      const float t = dht_.readTemperature();
      const float h = dht_.readHumidity();
      if (!isnan(t)) { temp_ = t; hasTemp_ = true; }  // 실패 시 직전 값 유지
      if (!isnan(h)) { hum_ = h; hasHum_ = true; }
      lastDht_ = now;
    }
    Sample s{temp_, hum_, soundFlag_ ? 1.0f : 0.0f};
    soundFlag_ = false;
    return s;
  }

  // 조이스틱 X 를 -1.0 ~ +1.0 으로 (데드존 ±0.25 안이면 0)
  float joyX() {
#ifdef NO_JOYSTICK
    return 0.0f;  // 조이스틱 미연결: 떠 있는 ADC 노이즈로 수동 모드에 빠지지 않게
#endif
    const int raw = analogRead(PIN_JOY_X);
    float v = (raw - joyCenter_) / 2048.0f;
    if (v > 1) v = 1;
    if (v < -1) v = -1;
    return fabsf(v) < 0.25f ? 0.0f : v;
  }
  bool joyPressed() {
#ifdef NO_JOYSTICK
    return false;
#endif
    return digitalRead(PIN_JOY_SW) == LOW;
  }

  void servoWrite(int angle) {
    angle = constrain(angle, 0, 180);
    if (angle == servoAngle_) return;
    const uint32_t us = map(angle, 0, 180, 500, 2400);  // SG90 펄스 폭
    ledcWrite(kServoCh, (uint32_t)(us * ((1UL << kServoBits) - 1) / 20000UL));
    servoAngle_ = angle;
  }
  int servoAngle() const { return servoAngle_; }

  // DHT 를 한 번이라도 읽기 전에는 temp/hum 이 초기값 0 → 이 값을 쓰면 안 됨
  bool ready() const { return hasTemp_ && hasHum_; }

 private:
  static constexpr int kServoCh = 0;
  static constexpr int kServoBits = 14;
  DHT dht_;
  float temp_ = 0, hum_ = 0;
  uint32_t lastDht_ = 0;
  bool hasTemp_ = false, hasHum_ = false;
  int servoAngle_ = -1;
  int joyCenter_ = 2048;
  static volatile bool soundFlag_;
  static void onSound();
};

volatile bool Sensors::soundFlag_ = false;

// ISR 은 클래스 밖(비-inline)에 정의해야 한다. 클래스 안 inline + IRAM_ATTR 이면
// xtensa 링커가 "dangerous relocation: l32r: literal placed after use" 로 실패함.
void IRAM_ATTR Sensors::onSound() { soundFlag_ = true; }
