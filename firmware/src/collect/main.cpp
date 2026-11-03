// [1단계] Edge Impulse 데이터 수집용 펌웨어
// 시리얼로 "temp,hum,sound" 를 10 Hz 로 출력 → PC 에서 edge-impulse-data-forwarder 실행
// ※ data forwarder 는 숫자 이외의 줄이 섞이면 안 되므로 로그 출력 금지
#include <Arduino.h>

#include "sensors.h"

Sensors sensors;
uint32_t next = 0;

void setup() {
  Serial.begin(115200);
  sensors.begin();
  sensors.servoWrite(0);
}

void loop() {
  const uint32_t now = millis();
  if ((int32_t)(now - next) < 0) return;
  next = now + SAMPLE_INTERVAL_MS;
  const Sample s = sensors.read();
  Serial.printf("%.1f,%.1f,%.0f\n", s.temp, s.hum, s.sound);
}
