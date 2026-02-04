#pragma once

#include <cstdint>
#include <driver/twai.h>
#include "include/config.h"

class RobStrideCAN {
 public:
  bool begin();
  void end();
  bool send_enable(uint8_t motor_id);
  bool send_cmd(uint8_t motor_id, float p, float v, float kp, float kd, float t);
  bool poll_rx();
  uint32_t rx_count = 0;
  uint32_t last_can_id = 0;
  uint16_t can_rx_flags = 0;

 private:
  bool started_ = false;
  void pack_mit(float p, float v, float kp, float kd, float t, uint8_t out[8]);
};
