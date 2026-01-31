#pragma once

#include <cstdint>

#include "include/config.h"
#include "ring_buffer.h"

struct SafetyState {
  bool estop[MAX_MOTORS] = {false};
  uint32_t last_sp_us[MAX_MOTORS] = {0};
  bool wd_tripped[MAX_MOTORS] = {false};
  bool use_limits[MAX_MOTORS] = {false};
};

class Safety {
 public:
  void init() {
    for (uint8_t i = 0; i < MAX_MOTORS; ++i) st.use_limits[i] = (USE_LIMITS != 0);
  }
  void update_last_sp(uint8_t idx, uint32_t t_us) { st.last_sp_us[idx] = t_us; }
  void set_estop(uint8_t idx, bool v) { st.estop[idx] = v; }
  void set_estop_all(bool v) { for (uint8_t i = 0; i < MAX_MOTORS; ++i) st.estop[i] = v; }
  void set_use_limits(uint8_t idx, bool v) { st.use_limits[idx] = v; }
  void set_use_limits_all(bool v) { for (uint8_t i = 0; i < MAX_MOTORS; ++i) st.use_limits[i] = v; }
  bool check_watchdog(uint8_t idx, uint32_t now_us) {
    bool tripped = (now_us - st.last_sp_us[idx]) > WATCHDOG_US;
    st.wd_tripped[idx] = tripped;
    return tripped;
  }
  bool within_limits(uint8_t idx, float pos) {
    if (!st.use_limits[idx]) return true;
    return pos >= SOFT_LIMIT_MIN && pos <= SOFT_LIMIT_MAX;
  }
  float clamp_pos(uint8_t idx, float pos) {
    if (!st.use_limits[idx]) return pos;
    if (pos < SOFT_LIMIT_MIN) return SOFT_LIMIT_MIN;
    if (pos > SOFT_LIMIT_MAX) return SOFT_LIMIT_MAX;
    return pos;
  }
  void apply_stop(float* kp, float* kd, float* v) {
    *v = 0.0f; *kp = 5.0f; *kd = 0.1f;
  }
  SafetyState st;
};
