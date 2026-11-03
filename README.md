# 스마트 룸 컨트롤러 (XIAO ESP32S3 + Edge Impulse + Supabase)

방 안의 **온도·습도·소리**를 ESP32-S3 가 10 Hz 로 읽고, Edge Impulse 로 학습한 모델이
방 상태를 **쾌적(calm) / 소음·활동(noisy) / 환기 필요(stuffy)** 로 분류해서 **서보(환기구)** 를 제어한다.
조이스틱으로 현장에서 수동 조작할 수 있고(버튼 = AI 자동 복귀), 상태가 바뀌거나 1분마다 Supabase 에 저장된다.

```
 DHT11 ─┐                                   조이스틱 ─┐ (수동 > 대시보드 > 자동)
 Sound ─┴▶ ESP32-S3 ─ 3초 윈도 ─▶ Edge Impulse 모델 ─▶ 다수결 ─▶ 서보 각도
                 │
                 └─ POST /api/telemetry (1초) ─▶ comm ─ws─▶ 대시보드 (localhost:8000)
                         ◀─ 대시보드 수동 각도 ──┘   │
                                    report 시 POST /events
                                                    ▼
                                         log (SQLite outbox) ─REST─▶ Supabase room_events
```

| 상태 | 서보 | 의미 |
|---|---|---|
| calm | 0° | 닫힘 |
| noisy | 45° | 반쯤 열림 (사람 활동) |
| stuffy | 90° | 활짝 (덥고 습함) |

## 배선 (Seeed XIAO ESP32S3, `firmware/include/pins.h`)
| 모듈 | 신호 → 보드 핀 | 전원 |
|---|---|---|
| DHT11 | DATA → D3 (GPIO4) | 3V3 |
| 사운드 센서 | DO → D5 (GPIO6) | 3V3 |
| 서보 SG90 | 주황 → D8 (GPIO7) | 빨강 VUSB(5V), 갈색 GND |
| 조이스틱 | VRx → D0 (GPIO1), SW → D1 (GPIO2) | **+5V 핀에 3V3** 연결 (5V 금지) |

모든 GND 는 브레드보드 GND 레일로 모아 XIAO GND 1개에 연결.

## 진행 순서
### 1. 데이터 수집 (Edge Impulse)
```bash
cd firmware && pio run -e collect -t upload
npm install -g edge-impulse-cli
edge-impulse-data-forwarder        # 축 이름: temp,hum,sound  (10 Hz 자동 감지)
```
Edge Impulse 프로젝트 이름은 **room** 으로. Data acquisition 에서 라벨별 2~3분씩 녹화:
`calm`(조용한 상태) · `noisy`(말하기·박수·음악) · `stuffy`(센서에 입김·손으로 감싸서 온습도 올리기)
> 수집 중에는 시리얼 모니터를 닫아야 함 (포트 점유)

### 2. 학습
Create impulse: Time series, window 3000 ms / increase 1000 ms → Processing **Flatten** →
Learning **Classification** (+ 여유 있으면 **Anomaly detection (K-means)**).
Deployment → **Arduino library** → zip 을 `firmware/lib/` 에 풀기 (헤더 `room_inferencing.h`).

### 3. 추론 펌웨어
`include/secrets.example.h` → `secrets.h` 복사 후 WiFi / PC IP 입력
```bash
pio run -e room -t upload && pio device monitor
```
라이브러리가 없으면 규칙 기반 분류기로 동작 (`model: rule` 로 표시) → 모델 적용 전후 비교 가능.

### 4. 서버 + DB
1. Supabase SQL Editor 에서 `supabase/schema.sql` 실행
2. `.env.example` → `.env` (Project URL, secret/service_role 키)
3. `docker compose up --build` → http://localhost:8000
4. Windows 방화벽에서 8000 포트 인바운드 허용
- ESP32 없이 화면/DB 확인: `python tools/fake_device.py`
- 8000/8001 포트가 이미 쓰이고 있으면 `.env` 에 `DASHBOARD_PORT` / `LOG_PORT` 지정 (예: 8010/8011)

### 4-1. WiFi 없이 USB 로 대시보드 연결 (시리얼 브리지)
```bash
pip install -r tools/requirements.txt
python tools/serial_bridge.py          # ESP32 포트 자동 검색, .env 의 DASHBOARD_PORT 사용
```
- 펌웨어(`room`, `room_sensors`)가 매 추론마다 `@{json}` 한 줄을 시리얼로 출력 → 브리지가 `/api/telemetry` 로 전달
  → report 시점이면 log → Supabase 저장. 대시보드 수동 각도는 시리얼(`O<각도>`)로 펌웨어에 되돌려줌
- 그 외 시리얼 출력(진단 펌웨어 `dhttest` 등)은 대시보드 **시리얼 로그** 패널에 실시간 표시
- USB 가 끊기면 자동 재연결. 업로드/`pio device monitor` 전에는 브리지를 Ctrl+C 로 종료 (포트 점유)

## CI (`.github/workflows/ci.yml`)
- comm / log / tools pytest (텔레메트리 수신, 로그 전달, 대시보드 수동 조작, 시리얼 로그, Supabase 업로드·재전송·URL 정규화, 시리얼 브리지 파싱)
- 펌웨어: `pio test -e native` (RoomLogic: 다수결, 로그 시점, 서보 제어권, 규칙 분류) + `room` / `room_sensors` / `collect` / `dhttest` 병렬 빌드 → 크기 리포트, `firmware.bin` 아티팩트 업로드
- docker compose 기동 → 대시보드 응답, telemetry → log 저장 경로 스모크 테스트

## 트러블슈팅
- 시리얼이 안 보이면: platformio.ini 에 `ARDUINO_USB_CDC_ON_BOOT=1` 이 있는지 확인. 업로드가 안 되면 BOOT 버튼을 누른 채 USB 연결 후 업로드
- Edge Impulse 라이브러리 컴파일 에러: Arduino core 3.x 에서 문제가 잦아 `espressif32@6.9.0`(core 2.x) 로 고정해 둠
- 조이스틱 중앙값은 부팅 시 자동 보정 → 전원 켤 때 스틱을 건드리지 말 것
- DHT11 은 1초 이하 간격으로 읽으면 NaN → 1.5초 간격으로 읽고 사이 값은 유지
