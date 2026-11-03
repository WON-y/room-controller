// 스마트 룸 컨트롤러 - 하드웨어 비의존 로직 (PC native 테스트 가능)
//  - 라벨 정의 / 라벨 → 서보 각도
//  - 예측 흔들림 방지 (최근 N회 다수결)
//  - 로그 전송 시점 판단 (상태 변경 or 주기)
//  - 서보 제어권 결정 (조이스틱 > 대시보드 > 자동)
//  - 규칙 기반 분류기 (Edge Impulse 모델이 없을 때 대체 + 비교 기준선)
#pragma once
#include <stdint.h>

namespace room {

enum Label : int { CALM = 0, NOISY = 1, STUFFY = 2, N_LABELS = 3 };

const char* labelName(int label);
int labelFromName(const char* name);  // Edge Impulse 라벨 문자열 → enum, 모르면 -1
int servoAngleFor(int label);         // 환기구 개방 각도

// 최근 kSize 번 예측의 다수결 (동률이면 가장 최근 값)
class Smoother {
 public:
  static constexpr int kSize = 3;
  // probs 를 주면 confidence() 계산에 사용 (nullptr 이면 해당 라벨 100% 로 간주)
  int push(int label, const float* probs = nullptr);
  // 다수결 라벨의 확률을 최근 예측들에 대해 평균 → 표시 라벨과 항상 일치하는 신뢰도
  float confidence(int label) const;
 private:
  int hist_[kSize] = {0};
  float probs_[kSize][N_LABELS] = {};
  int n_ = 0, idx_ = 0;
};

// 로그 전송 여부: 상태가 바뀌면 "change", periodMs 경과 시 "periodic", 아니면 nullptr
class Reporter {
 public:
  explicit Reporter(uint32_t periodMs = 60000) : periodMs_(periodMs) {}
  const char* check(int label, uint32_t nowMs);
 private:
  uint32_t periodMs_;
  int last_ = -1;
  uint32_t lastMs_ = 0;
};

// 서보 제어권. 조이스틱을 기울이면 수동(joystick), 버튼 누르면 자동 복귀.
enum class Control : int { AUTO = 0, DASHBOARD = 1, JOYSTICK = 2 };
const char* controlName(Control c);

class ServoController {
 public:
  // joyX: -1~1 (0 = 중립), pressed: 조이스틱 버튼, dashboardAngle: -1 이면 없음
  int update(int autoAngle, float joyX, bool pressed, int dashboardAngle, float dtSec);
  Control control() const { return control_; }
 private:
  Control control_ = Control::AUTO;
  float manualAngle_ = 0;
  bool wasPressed_ = false;
};

// 윈도 특징: temp/hum 평균, sound 는 감지 비율(0~1)
struct Features {
  float temp, hum, soundRate;
};

int ruleClassify(const Features& f, float probs[N_LABELS]);

}  // namespace room
