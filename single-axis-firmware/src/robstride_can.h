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
  bool send_vel(uint8_t motor_id, float v);
  bool request_pos(uint8_t motor_id);
  bool send_stop(uint8_t motor_id);
  bool poll_rx();
  uint32_t rx_count = 0;
  uint32_t last_can_id = 0;
  uint16_t can_rx_flags = 0;
  float last_pos_rad = 0.0f;
  bool last_pos_valid = false;

 private:
  bool started_ = false;
  bool tx_frame(uint32_t id, bool ext, const uint8_t data[8], uint8_t dlc);
  uint32_t make_rs_id(uint8_t type, uint8_t motor_id);
  void pack_mit(float p, float v, float kp, float kd, float t, uint8_t out[8]);
};
