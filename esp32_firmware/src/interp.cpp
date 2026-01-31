#include "interp.h"

#include <cstddef>
#include <cstdint>

#include "include/config.h"

static inline float clampf(float x, float a, float b) { return x < a ? a : (x > b ? b : x); }

bool Interpolator::compute(const RingBuffer& rb, uint32_t t_us, RefState* out) {
  if (rb.size() == 0) return false;
  // Find segment surrounding t_us
  Setpoint s0{};
  Setpoint s1{};
  bool has0 = false;
  bool has1 = false;
  for (size_t i = 0; i < rb.size(); ++i) {
    Setpoint sp{};
    rb.peek(i, &sp);
    if (sp.t_us <= t_us) {
      s0 = sp;
      has0 = true;
    } else {
      s1 = sp;
      has1 = true;
      break;
    }
  }
  if (!has0) {
    rb.peek(0, &s1);
    out->pos = s1.pos;
    out->vel = 0.0f;
    out->acc = 0.0f;
    out->flags = s1.flags;
    return true;
  }
  if (!has1) {
    out->pos = s0.pos;
    out->vel = 0.0f;
    out->acc = 0.0f;
    out->flags = s0.flags;
    return true;
  }

  if (s0.flags & 0x1) {  // hold
    out->pos = s0.pos;
    out->vel = 0.0f;
    out->acc = 0.0f;
    out->flags = s0.flags;
    return true;
  }

  float t0 = s0.t_us * 1e-6f;
  float t1 = s1.t_us * 1e-6f;
  float t = t_us * 1e-6f;
  float h = t1 - t0;
  if (h <= 1e-6f) {
    out->pos = s1.pos;
    out->vel = 0.0f;
    out->acc = 0.0f;
    out->flags = s1.flags;
    return true;
  }
  float tau = (t - t0) / h;
  float p0 = s0.pos;
  float p1 = s1.pos;
  float v0 = s0.vel;
  float v1 = s1.vel;
  // Cubic Hermite basis
  float h00 = 2 * tau * tau * tau - 3 * tau * tau + 1;
  float h10 = tau * tau * tau - 2 * tau * tau + tau;
  float h01 = -2 * tau * tau * tau + 3 * tau * tau;
  float h11 = tau * tau * tau - tau * tau;
  float pos = h00 * p0 + h10 * h * v0 + h01 * p1 + h11 * h * v1;
  float vel = (6 * (tau * tau) - 6 * tau) * (p0 - p1) / h + (3 * tau * tau - 4 * tau + 1) * v0 + (3 * tau * tau - 2 * tau) * v1;
  // Simple accel estimate by clamping derivative change
  float acc = clampf((v1 - v0) / h, -MAX_ACC, MAX_ACC);
  // Limits
  vel = clampf(vel, -MAX_VEL, MAX_VEL);
  out->pos = pos;
  out->vel = vel;
  out->acc = acc;
  out->flags = (uint16_t)(s0.flags | s1.flags);
  return true;
}
