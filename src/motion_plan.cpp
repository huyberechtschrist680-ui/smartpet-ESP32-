#include "motion_plan.h"

#include <math.h>

#include "pet_state.h"

namespace
{
  constexpr float kTwoPi = 6.28318530718f;

  ServoAngles makeAngles(int left, int right)
  {
    ServoAngles angles;
    angles.left = left;
    angles.right = right;
    return angles;
  }

  int roundedAngle(float value)
  {
    return static_cast<int>(lroundf(value));
  }

  float progress(uint32_t elapsedMs, uint32_t durationMs)
  {
    if (durationMs == 0 || elapsedMs >= durationMs)
    {
      return 1.0f;
    }
    return static_cast<float>(elapsedMs) / static_cast<float>(durationMs);
  }

} // namespace

ServoAngles getServoAngles(MotionMode mode, uint32_t elapsedMs) // 借助三角函数周期性完成动作，以时间为基准
{
  const uint32_t duration = motionDurationMs(mode);
  if (mode == MotionMode::Null || duration == 0 || elapsedMs >= duration)
  {
    return makeAngles(0, 0);
  }

  const float t = progress(elapsedMs, duration);

  switch (mode)
  {
  case MotionMode::Play:
  {
    const int angle = roundedAngle(45.0f * sinf(kTwoPi * 3.0f * t));
    return makeAngles(angle, angle);
  }
  case MotionMode::Idle:
  {
    const int angle = roundedAngle(-30.0f + 30.0f * cosf(kTwoPi * 3.0f * t));
    return makeAngles(angle, angle);
  }
  case MotionMode::Tire:
  {
    const int right = roundedAngle(40.0f - 10.0f * cosf(kTwoPi * 2.0f * t));
    return makeAngles(0, right);
  }
  case MotionMode::Forward:
  {
    const int left = roundedAngle(-35.0f + 35.0f * cosf(kTwoPi * 3.0f * t));
    return makeAngles(left, -left);
  }
  case MotionMode::Null:
  default:
    return makeAngles(0, 0);
  }
}
