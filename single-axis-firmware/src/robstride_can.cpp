#include "robstride_can.h"

bool RobStrideCAN::begin() {
  if (started_) return true;
  twai_general_config_t g_config = TWAI_GENERAL_CONFIG_DEFAULT((gpio_num_t)TWAI_TX_PIN, (gpio_num_t)TWAI_RX_PIN, TWAI_MODE_NORMAL);
  twai_timing_config_t t_config;
#if TWAI_BAUD == 1000000
  t_config = TWAI_TIMING_CONFIG_1MBITS();
#elif TWAI_BAUD == 500000
  t_config = TWAI_TIMING_CONFIG_500KBITS();
#elif TWAI_BAUD == 250000
  t_config = TWAI_TIMING_CONFIG_250KBITS();
#else
  t_config = TWAI_TIMING_CONFIG_1MBITS();
#endif
  twai_filter_config_t f_config = TWAI_FILTER_CONFIG_ACCEPT_ALL();
  if (twai_driver_install(&g_config, &t_config, &f_config) != ESP_OK) return false;
  if (twai_start() != ESP_OK) return false;
  started_ = true;
  return true;
}

void RobStrideCAN::end() {
  if (!started_) return;
  twai_stop();
  twai_driver_uninstall();
  started_ = false;
}

static inline uint16_t float_to_uint(float x, float x_min, float x_max, int bits) {
  if (x < x_min) x = x_min;
  if (x > x_max) x = x_max;
  return (uint16_t)((x - x_min) * ((float)((1 << bits) - 1)) / (x_max - x_min));
}

void RobStrideCAN::pack_mit(float p, float v, float kp, float kd, float t, uint8_t out[8]) {
  // Pack into 8 bytes with bit fields: p:16, v:12, kp:12, kd:12, t:12
  uint16_t p_u = float_to_uint(p, POS_MIN, POS_MAX, 16);
  uint16_t v_u = float_to_uint(v, VEL_MIN, VEL_MAX, 12);
  uint16_t kp_u = float_to_uint(kp, KP_MIN, KP_MAX, 12);
  uint16_t kd_u = float_to_uint(kd, KD_MIN, KD_MAX, 12);
  uint16_t t_u = float_to_uint(t, T_MIN, T_MAX, 12);
  out[0] = p_u >> 8;
  out[1] = p_u & 0xFF;
  out[2] = v_u >> 4;
  out[3] = ((v_u & 0xF) << 4) | (kp_u >> 8);
  out[4] = kp_u & 0xFF;
  out[5] = kd_u >> 4;
  out[6] = ((kd_u & 0xF) << 4) | (t_u >> 8);
  out[7] = t_u & 0xFF;
}

bool RobStrideCAN::send_enable(uint8_t motor_id) {
#if ROBSTRIDE_MODE
  // RobStride enable sequence:
  // 1) Type 0x12 write index 0x7005 = 1
  // 2) Type 0x03 (enable) with 8 zero bytes
  uint8_t data1[8] = {0x05, 0x70, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00};
  (void)tx_frame(make_rs_id(0x12, motor_id), true, data1, 8);
  uint8_t zeros[8] = {0};
  return tx_frame(make_rs_id(0x03, motor_id), true, zeros, 8);
#else
  if (!started_) return false;
  twai_message_t msg = {};
  msg.identifier = 0x200 + (motor_id & 0x1F);
  msg.extd = CAN_USE_EXTENDED ? 1 : 0;
  msg.data_length_code = 8;
  for (int i = 0; i < 8; ++i) msg.data[i] = 0xFF;
  return twai_transmit(&msg, pdMS_TO_TICKS(5)) == ESP_OK;
#endif
}

bool RobStrideCAN::send_cmd(uint8_t motor_id, float p, float v, float kp, float kd, float t) {
#if ROBSTRIDE_MODE
  // RobStride position write: Type 0x12, index 0x7016, value float32 LE radians
  (void)v; (void)kp; (void)kd; (void)t;
  uint8_t data[8];
  data[0] = (uint8_t)(ROBSTRIDE_IDX_POS & 0xFF);
  data[1] = (uint8_t)((ROBSTRIDE_IDX_POS >> 8) & 0xFF);
  data[2] = 0x00; data[3] = 0x00;
  union { float f; uint8_t b[4]; } u; u.f = p;
  data[4] = u.b[0]; data[5] = u.b[1]; data[6] = u.b[2]; data[7] = u.b[3];
  return tx_frame(make_rs_id(0x12, motor_id), true, data, 8);
#else
  if (!started_) return false;
  uint8_t data[8];
  pack_mit(p, v, kp, kd, t, data);
  twai_message_t msg = {};
  msg.identifier = 0x200 + (motor_id & 0x1F);
  msg.extd = CAN_USE_EXTENDED ? 1 : 0;
  msg.data_length_code = 8;
  for (int i = 0; i < 8; ++i) msg.data[i] = data[i];
  return twai_transmit(&msg, pdMS_TO_TICKS(5)) == ESP_OK;
#endif
}

bool RobStrideCAN::send_vel(uint8_t motor_id, float v) {
#if ROBSTRIDE_MODE
  uint8_t data[8];
  data[0] = (uint8_t)(ROBSTRIDE_IDX_VEL & 0xFF);
  data[1] = (uint8_t)((ROBSTRIDE_IDX_VEL >> 8) & 0xFF);
  data[2] = 0x00; data[3] = 0x00;
  union { float f; uint8_t b[4]; } u; u.f = v;
  data[4] = u.b[0]; data[5] = u.b[1]; data[6] = u.b[2]; data[7] = u.b[3];
  return tx_frame(make_rs_id(0x12, motor_id), true, data, 8);
#else
  (void)motor_id; (void)v;
  return false;
#endif
}

bool RobStrideCAN::request_pos(uint8_t motor_id) {
#if ROBSTRIDE_MODE
  uint8_t data[8] = {};
  data[0] = (uint8_t)(ROBSTRIDE_IDX_POS & 0xFF);
  data[1] = (uint8_t)((ROBSTRIDE_IDX_POS >> 8) & 0xFF);
  return tx_frame(make_rs_id(0x11, motor_id), true, data, 8);
#else
  (void)motor_id;
  return false;
#endif
}

bool RobStrideCAN::poll_rx() {
  if (!started_) return false;
  twai_message_t msg;
  esp_err_t r = twai_receive(&msg, 0);
  if (r == ESP_OK) {
    rx_count++;
    last_can_id = msg.identifier;
    can_rx_flags = 0;
#if ROBSTRIDE_MODE
    if (msg.extd && msg.data_length_code >= 8) {
      uint8_t idx0 = msg.data[0];
      uint8_t idx1 = msg.data[1];
      uint16_t idx = (uint16_t)idx0 | ((uint16_t)idx1 << 8);
      if (idx == ROBSTRIDE_IDX_POS) {
        union { float f; uint8_t b[4]; } u;
        u.b[0] = msg.data[4];
        u.b[1] = msg.data[5];
        u.b[2] = msg.data[6];
        u.b[3] = msg.data[7];
        last_pos_rad = u.f;
        last_pos_valid = true;
      }
    }
#endif
    return true;
  }
  return false;
}

bool RobStrideCAN::tx_frame(uint32_t id, bool ext, const uint8_t data[8], uint8_t dlc) {
  if (!started_) return false;
  twai_message_t msg = {};
  msg.identifier = id;
  msg.extd = ext ? 1 : 0;
  msg.data_length_code = dlc;
  for (int i = 0; i < dlc && i < 8; ++i) msg.data[i] = data[i];
  return twai_transmit(&msg, pdMS_TO_TICKS(5)) == ESP_OK;
}

uint32_t RobStrideCAN::make_rs_id(uint8_t type, uint8_t motor_id) {
  // Extended 29-bit ID: [ mode(5) | host_id(16) | motor_id(8) ]
  uint32_t id = ((uint32_t)(type & 0x1F) << 24) |
                ((uint32_t)(ROBSTRIDE_HOST_ID & 0xFFFF) << 8) |
                (uint32_t)(motor_id & 0xFF);
  return id;
}

bool RobStrideCAN::send_stop(uint8_t motor_id) {
#if ROBSTRIDE_MODE
  uint8_t zeros[8] = {0};
  return tx_frame(make_rs_id(0x04, motor_id), true, zeros, 8);
#else
  return false;
#endif
}
