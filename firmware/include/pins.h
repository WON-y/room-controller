// Seeed XIAO ESP32S3 핀 배치 (코드의 숫자는 GPIO 번호, 보드 인쇄는 D 번호)
//
//  모듈            모듈 핀     →  보드 인쇄 (GPIO)   전원
//  DHT11           DATA(S)    →  D3  (GPIO4)        3V3
//  사운드 센서      DO         →  D5  (GPIO6)        3V3
//  서보 (SG90)     주황(신호)  →  D8  (GPIO7)        빨강 VUSB(5V), 갈색 GND
//  조이스틱         VRx        →  D0  (GPIO1, ADC)   +5V 핀에 3V3 연결 (5V 금지! ADC 최대 3.3V)
//                  SW         →  D1  (GPIO2)
//                  VRy        →  연결 안 함
//  ※ D2(GPIO3)는 부팅 모드 핀이라 사용하지 않음
#pragma once

#define PIN_DHT 4
#define PIN_SOUND 6
#define PIN_SERVO 7
#define PIN_JOY_X 1
#define PIN_JOY_SW 2

#define SAMPLE_INTERVAL_MS 100   // 10 Hz (Edge Impulse 수집 주파수와 동일해야 함)
#define DHT_INTERVAL_MS 1500     // DHT11 은 1초에 1번 이상 읽으면 안 됨
