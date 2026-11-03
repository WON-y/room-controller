#include <unity.h>

#include "RoomLogic.h"

using namespace room;

void setUp() {}
void tearDown() {}

void test_labels_and_servo() {
  TEST_ASSERT_EQUAL(STUFFY, labelFromName("stuffy"));
  TEST_ASSERT_EQUAL(-1, labelFromName("empty"));
  TEST_ASSERT_EQUAL(0, servoAngleFor(CALM));
  TEST_ASSERT_EQUAL(90, servoAngleFor(STUFFY));
  TEST_ASSERT_TRUE(servoAngleFor(CALM) < servoAngleFor(NOISY));
}

void test_smoother_majority() {
  Smoother s;
  TEST_ASSERT_EQUAL(CALM, s.push(CALM));
  TEST_ASSERT_EQUAL(CALM, s.push(CALM));
  TEST_ASSERT_EQUAL(CALM, s.push(NOISY));   // 한 번 튀는 값은 무시
  TEST_ASSERT_EQUAL(NOISY, s.push(NOISY));  // 두 번 연속이면 반영
}

void test_reporter() {
  Reporter r(60000);
  TEST_ASSERT_EQUAL_STRING("change", r.check(CALM, 1000));
  TEST_ASSERT_NULL(r.check(CALM, 2000));
  TEST_ASSERT_EQUAL_STRING("periodic", r.check(CALM, 61000));
  TEST_ASSERT_NULL(r.check(CALM, 62000));
  TEST_ASSERT_EQUAL_STRING("change", r.check(STUFFY, 63000));
}

void test_servo_controller() {
  ServoController c;
  TEST_ASSERT_EQUAL(45, c.update(45, 0, false, -1, 0.05f));          // 자동
  TEST_ASSERT_EQUAL(120, c.update(45, 0, false, 120, 0.05f));        // 대시보드 우선
  TEST_ASSERT_TRUE(c.control() == Control::DASHBOARD);
  int a = 0;
  for (int i = 0; i < 20; ++i) a = c.update(45, 1.0f, false, 120, 0.05f);  // 1초 동안 오른쪽
  TEST_ASSERT_EQUAL(180, a);                                          // 120 + 90 → 180 상한
  TEST_ASSERT_TRUE(c.control() == Control::JOYSTICK);
  TEST_ASSERT_EQUAL(180, c.update(45, 0, false, 120, 0.05f));        // 스틱 놓아도 유지
  c.update(45, 0, true, -1, 0.05f);                                   // 버튼 → 자동 복귀
  TEST_ASSERT_TRUE(c.control() == Control::AUTO);
  TEST_ASSERT_EQUAL(90, c.update(90, 0, false, -1, 0.05f));
}

void test_rule_classifier() {
  float p[N_LABELS];
  TEST_ASSERT_EQUAL(CALM, ruleClassify({23, 45, 0.0f}, p));
  TEST_ASSERT_EQUAL(NOISY, ruleClassify({23, 45, 0.4f}, p));
  TEST_ASSERT_EQUAL(STUFFY, ruleClassify({29, 75, 0.1f}, p));
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_labels_and_servo);
  RUN_TEST(test_smoother_majority);
  RUN_TEST(test_reporter);
  RUN_TEST(test_servo_controller);
  RUN_TEST(test_rule_classifier);
  return UNITY_END();
}
