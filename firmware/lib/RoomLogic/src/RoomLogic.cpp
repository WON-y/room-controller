#include "RoomLogic.h"

#include <string.h>

namespace room {

static const char* kNames[N_LABELS] = {"calm", "noisy", "stuffy"};
static const int kAngles[N_LABELS] = {0, 45, 90};  // 쾌적=닫힘, 소음(활동)=반, 덥고 습함=활짝

const char* labelName(int label) { return (label >= 0 && label < N_LABELS) ? kNames[label] : "unknown"; }

int labelFromName(const char* name) {
  for (int i = 0; i < N_LABELS; ++i)
    if (strcmp(name, kNames[i]) == 0) return i;
  return -1;
}

int servoAngleFor(int label) { return (label >= 0 && label < N_LABELS) ? kAngles[label] : 0; }

int Smoother::push(int label, const float* probs) {
  hist_[idx_] = label;
  for (int l = 0; l < N_LABELS; ++l) probs_[idx_][l] = probs ? probs[l] : (l == label ? 1.0f : 0.0f);
  idx_ = (idx_ + 1) % kSize;
  if (n_ < kSize) ++n_;
  int counts[N_LABELS] = {0};
  for (int i = 0; i < n_; ++i)
    if (hist_[i] >= 0 && hist_[i] < N_LABELS) ++counts[hist_[i]];
  int best = label;
  for (int l = 0; l < N_LABELS; ++l)
    if (counts[l] > counts[best]) best = l;
  return best;
}

float Smoother::confidence(int label) const {
  if (n_ == 0 || label < 0 || label >= N_LABELS) return 0.0f;
  float sum = 0;
  for (int i = 0; i < n_; ++i) sum += probs_[i][label];
  return sum / n_;
}

const char* Reporter::check(int label, uint32_t nowMs) {
  if (label != last_) {
    last_ = label;
    lastMs_ = nowMs;
    return "change";
  }
  if (nowMs - lastMs_ >= periodMs_) {
    lastMs_ = nowMs;
    return "periodic";
  }
  return nullptr;
}

const char* controlName(Control c) {
  switch (c) {
    case Control::AUTO: return "auto";
    case Control::DASHBOARD: return "dashboard";
    case Control::JOYSTICK: return "joystick";
  }
  return "?";
}

int ServoController::update(int autoAngle, float joyX, bool pressed, int dashboardAngle, float dtSec) {
  const bool click = pressed && !wasPressed_;
  wasPressed_ = pressed;
  if (click) control_ = Control::AUTO;  // 버튼: 자동 복귀

  if (joyX != 0.0f) {
    if (control_ != Control::JOYSTICK) {
      // 현재 각도에서 이어서 움직이도록
      manualAngle_ = (control_ == Control::DASHBOARD && dashboardAngle >= 0) ? dashboardAngle : autoAngle;
      control_ = Control::JOYSTICK;
    }
    manualAngle_ += joyX * 90.0f * dtSec;  // 끝까지 기울이면 초당 90°
    if (manualAngle_ < 0) manualAngle_ = 0;
    if (manualAngle_ > 180) manualAngle_ = 180;
  } else if (control_ != Control::JOYSTICK) {
    control_ = dashboardAngle >= 0 ? Control::DASHBOARD : Control::AUTO;
  }

  switch (control_) {
    case Control::JOYSTICK: return (int)(manualAngle_ + 0.5f);
    case Control::DASHBOARD: return dashboardAngle;
    default: return autoAngle;
  }
}

int ruleClassify(const Features& f, float probs[N_LABELS]) {
  int l;
  if (f.hum >= 70.0f || f.temp >= 28.0f) l = STUFFY;
  else if (f.soundRate >= 0.10f) l = NOISY;  // 3초 중 0.3초 이상 소리 (박수 몇 번이면 넘음)
  else l = CALM;
  for (int i = 0; i < N_LABELS; ++i) probs[i] = (i == l) ? 0.8f : 0.1f;
  return l;
}

}  // namespace room
