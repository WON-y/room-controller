// DHT11 배선 진단용 펌웨어 (pio run -e dhttest -t upload)
//  - D3 대기 전압: 모듈 내부 풀업 때문에 정상이면 HIGH
//  - 모든 D 핀 대기 상태 스캔 → DATA 선이 어느 핀에 꽂혔는지 추정
//  - DHT11 / DHT22 두 방식으로 읽기 시도
#include <Arduino.h>
#include <DHT.h>

#include "pins.h"

struct P { const char* name; int gpio; };
static const P kPins[] = {{"D0", 1}, {"D1", 2}, {"D3", 4}, {"D4", 5}, {"D5", 6},
                          {"D6", 43}, {"D7", 44}, {"D8", 7}, {"D9", 8}, {"D10", 9}};

DHT dht11(PIN_DHT, DHT11);
DHT dht22(PIN_DHT, DHT22);

void setup() {
  Serial.begin(115200);
  delay(1500);
  dht11.begin();
}

void loop() {
  Serial.println("\n==== DHT 진단 ====");
  // 1) 각 핀 대기 상태 (내부 풀다운으로 당겨도 HIGH 면 외부에서 HIGH 를 걸고 있음)
  Serial.print("핀 스캔(풀다운 기준, HIGH=외부 전압 있음): ");
  for (const P& p : kPins) {
    pinMode(p.gpio, INPUT_PULLDOWN);
    delay(2);
    Serial.printf("%s=%s ", p.name, digitalRead(p.gpio) ? "H" : "L");
  }
  Serial.println();

  // 2) DHT11 읽기
  pinMode(PIN_DHT, INPUT_PULLUP);
  delay(10);
  float t = dht11.readTemperature(false, true);
  float h = dht11.readHumidity();
  Serial.printf("DHT11 읽기: T=%s H=%s\n", isnan(t) ? "실패" : String(t, 1).c_str(),
                isnan(h) ? "실패" : String(h, 0).c_str());

  // 3) 혹시 DHT22(AM2302) 모듈인지
  delay(2100);
  dht22.begin();
  t = dht22.readTemperature(false, true);
  h = dht22.readHumidity();
  Serial.printf("DHT22 방식: T=%s H=%s\n", isnan(t) ? "실패" : String(t, 1).c_str(),
                isnan(h) ? "실패" : String(h, 0).c_str());
  delay(2100);
}
