#pragma once

#include <cstdint>
#include "ring_buffer.h"

struct RefState {
  float pos;
  float vel;
  float acc;
  uint16_t flags;
};

class Interpolator {
 public:
  // Compute reference at time t_us from a ring buffer of future setpoints
  bool compute(const RingBuffer& rb, uint32_t t_us, RefState* out);
};
