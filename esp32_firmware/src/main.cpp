#include <cmath>
#include <cstdlib>

#include "esp_attr.h"
#include "sys/cdefs.h"
#include "esp_err.h"
#include "driver/rmt_encoder.h"
#include "driver/rmt_tx.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "include/config.h"
#include "protocol_serial.h"
#include "ring_buffer.h"
#include "interp.h"
#include "robstride_can.h"
#include "safety.h"

#ifndef RMT_ENCODER_FUNC_ATTR
#define RMT_ENCODER_FUNC_ATTR
#endif

#ifndef PI
#define PI 3.14159265358979323846f
#endif

static SerialProtocol proto;
static RobStrideCAN canbus;
static Interpolator interp;
static Safety safety;

static RingBuffer buffers[MAX_MOTORS];
static uint8_t motor_ids[MAX_MOTORS];
static uint8_t motors_used = 1;  // start with 1 motor
static float pos_offset[MAX_MOTORS];
static bool calib_active[MAX_MOTORS];
static uint32_t calib_start_us[MAX_MOTORS];
static bool buffer_underrun[MAX_MOTORS];
static uint16_t last_error_code[MAX_MOTORS];
static float last_ref_pos[MAX_MOTORS];
static bool last_ref_valid[MAX_MOTORS];

static const uint16_t ERR_BUFFER_UNDERRUN = 1;
static const uint16_t ERR_WATCHDOG_TIMEOUT = 2;
static const uint16_t ERR_CAN_TX_FAILED = 3;
static const uint16_t ERR_INTERP_EMPTY = 4;
static int64_t led_pulse_until_us = 0;
static rmt_channel_handle_t led_channel = nullptr;
static rmt_encoder_handle_t led_encoder = nullptr;

typedef struct {
  rmt_encoder_t base;
  rmt_encoder_handle_t bytes_encoder;
  rmt_encoder_handle_t copy_encoder;
  int state;
  rmt_symbol_word_t reset_code;
} led_strip_encoder_t;

RMT_ENCODER_FUNC_ATTR
static size_t led_strip_encode(rmt_encoder_t* encoder,
                               rmt_channel_handle_t channel,
                               const void* primary_data,
                               size_t data_size,
                               rmt_encode_state_t* ret_state) {
  led_strip_encoder_t* led_enc = __containerof(encoder, led_strip_encoder_t, base);
  rmt_encode_state_t session_state = RMT_ENCODING_RESET;
  uint32_t state = 0;
  size_t encoded_symbols = 0;
  switch (led_enc->state) {
    case 0:
      encoded_symbols += led_enc->bytes_encoder->encode(led_enc->bytes_encoder, channel, primary_data, data_size, &session_state);
      if (session_state & RMT_ENCODING_COMPLETE) {
        led_enc->state = 1;
      }
      if (session_state & RMT_ENCODING_MEM_FULL) {
        state |= RMT_ENCODING_MEM_FULL;
        break;
      }
      [[fallthrough]];
    case 1:
      encoded_symbols += led_enc->copy_encoder->encode(led_enc->copy_encoder, channel, &led_enc->reset_code, sizeof(led_enc->reset_code), &session_state);
      if (session_state & RMT_ENCODING_COMPLETE) {
        state |= RMT_ENCODING_COMPLETE;
        led_enc->state = RMT_ENCODING_RESET;
      }
      if (session_state & RMT_ENCODING_MEM_FULL) {
        state |= RMT_ENCODING_MEM_FULL;
      }
      break;
  }
  *ret_state = static_cast<rmt_encode_state_t>(state);
  return encoded_symbols;
}

static esp_err_t led_strip_encoder_del(rmt_encoder_t* encoder) {
  led_strip_encoder_t* led_enc = __containerof(encoder, led_strip_encoder_t, base);
  rmt_del_encoder(led_enc->bytes_encoder);
  rmt_del_encoder(led_enc->copy_encoder);
  free(led_enc);
  return ESP_OK;
}

RMT_ENCODER_FUNC_ATTR
static esp_err_t led_strip_encoder_reset(rmt_encoder_t* encoder) {
  led_strip_encoder_t* led_enc = __containerof(encoder, led_strip_encoder_t, base);
  rmt_encoder_reset(led_enc->bytes_encoder);
  rmt_encoder_reset(led_enc->copy_encoder);
  led_enc->state = RMT_ENCODING_RESET;
  return ESP_OK;
}

static esp_err_t led_strip_new_encoder(rmt_encoder_handle_t* ret_encoder) {
  led_strip_encoder_t* led_enc = static_cast<led_strip_encoder_t*>(rmt_alloc_encoder_mem(sizeof(led_strip_encoder_t)));
  if (!led_enc) return ESP_ERR_NO_MEM;
  led_enc->base.encode = led_strip_encode;
  led_enc->base.del = led_strip_encoder_del;
  led_enc->base.reset = led_strip_encoder_reset;
  led_enc->state = RMT_ENCODING_RESET;

  rmt_bytes_encoder_config_t bytes_config = {};
  bytes_config.bit0.duration0 = 3;
  bytes_config.bit0.level0 = 1;
  bytes_config.bit0.duration1 = 9;
  bytes_config.bit0.level1 = 0;
  bytes_config.bit1.duration0 = 9;
  bytes_config.bit1.level0 = 1;
  bytes_config.bit1.duration1 = 3;
  bytes_config.bit1.level1 = 0;
  bytes_config.flags.msb_first = 1;
  if (rmt_new_bytes_encoder(&bytes_config, &led_enc->bytes_encoder) != ESP_OK) {
    free(led_enc);
    return ESP_FAIL;
  }
  rmt_copy_encoder_config_t copy_config = {};
  if (rmt_new_copy_encoder(&copy_config, &led_enc->copy_encoder) != ESP_OK) {
    rmt_del_encoder(led_enc->bytes_encoder);
    free(led_enc);
    return ESP_FAIL;
  }
  led_enc->reset_code = (rmt_symbol_word_t){};
  led_enc->reset_code.duration0 = 250;
  led_enc->reset_code.level0 = 0;
  led_enc->reset_code.duration1 = 250;
  led_enc->reset_code.level1 = 0;
  *ret_encoder = &led_enc->base;
  return ESP_OK;
}

static void led_set_rgb(uint8_t r, uint8_t g, uint8_t b) {
#if LED_GPIO >= 0
  if (led_channel && led_encoder) {
    uint8_t grb[3] = {g, r, b};
    rmt_transmit_config_t tx_config = {};
    tx_config.loop_count = 0;
    if (rmt_transmit(led_channel, led_encoder, grb, sizeof(grb), &tx_config) == ESP_OK) {
      rmt_tx_wait_all_done(led_channel, pdMS_TO_TICKS(10));
    }
  }
#endif
}

static void led_pulse(uint8_t r, uint8_t g, uint8_t b) {
#if LED_GPIO >= 0
  uint32_t now = (uint32_t)micros();
  led_pulse_until_us = (int64_t)now + LED_PULSE_US;
  led_set_rgb(r, g, b);
#endif
}

// Time alignment
static bool time_aligned = false;
static int64_t t_offset = 0;  // local_us - traj_us

static inline uint32_t micros32() { return (uint32_t)micros(); }

static void handle_setpoints(uint32_t ts_us, const Setpoint* sps, uint8_t count) {
  // Align time on first packet
  uint32_t now_us = micros32();
  if (!time_aligned) {
    t_offset = (int64_t)now_us - (int64_t)ts_us;
    time_aligned = true;
  }
  // Push into buffers per motor
  for (uint8_t i = 0; i < count; ++i) {
    uint8_t mid = sps[i].motor_id;
    // Find or assign index
    uint8_t idx = 0;
    bool found = false;
    for (uint8_t k = 0; k < motors_used; ++k) {
      if (motor_ids[k] == mid) { idx = k; found = true; break; }
    }
    if (!found) {
      if (motors_used < MAX_MOTORS) {
        idx = motors_used++;
      } else {
        idx = 0;  // overwrite first if overflow
      }
      motor_ids[idx] = mid;
    }
    Setpoint sp = sps[i];
    if (!safety.within_limits(idx, sp.pos)) {
      continue;
    }
    buffers[idx].push(sp);
    safety.update_last_sp(idx, sp.t_us);
  }
}

static void handle_command(uint8_t cmd, uint8_t motor_id) {
  led_pulse(LED_CMD_R, LED_CMD_G, LED_CMD_B);
  switch (cmd) {
    case 1:  // enable
      canbus.send_enable(motor_id);
      break;
    case 2:  // disable
      for (uint8_t i = 0; i < motors_used; ++i) {
        if (motor_id == 0 || motor_ids[i] == motor_id) safety.set_estop(i, true);
      }
      if (motor_id != 0) canbus.send_stop(motor_id);
      break;
    case 3:  // stop
      for (uint8_t i = 0; i < motors_used; ++i) {
        if (motor_id == 0 || motor_ids[i] == motor_id) safety.set_estop(i, true);
      }
      if (motor_id != 0) canbus.send_stop(motor_id);
      break;
    case 4:  // zero offset (software only placeholder)
      for (uint8_t i = 0; i < motors_used; ++i) {
        if (motor_id == 0 || motor_ids[i] == motor_id) {
          // Set current reference position as zero offset
          RefState ref{};
          uint32_t now = micros32();
          uint32_t traj_now = time_aligned ? (now - (uint32_t)t_offset) : 0;
          if (interp.compute(buffers[i], traj_now, &ref)) {
            pos_offset[i] = ref.pos;
          } else {
            pos_offset[i] = 0.0f;
          }
        }
      }
      break;
    case 5:  // ping
      break;
    case 6:  // home
      for (uint8_t i = 0; i < motors_used; ++i) {
        if (motor_id == 0 || motor_ids[i] == motor_id) {
          // Software home: set current position as zero
          RefState ref{};
          uint32_t now = micros32();
          uint32_t traj_now = time_aligned ? (now - (uint32_t)t_offset) : 0;
          if (interp.compute(buffers[i], traj_now, &ref)) {
            pos_offset[i] = ref.pos;
          } else {
            pos_offset[i] = 0.0f;
          }
        }
      }
      break;
    case 7:  // calibrate
      for (uint8_t i = 0; i < motors_used; ++i) {
        if (motor_id == 0 || motor_ids[i] == motor_id) {
          calib_active[i] = true;
          calib_start_us[i] = micros32();
        }
      }
      break;
  }
}

void setup() {
  proto.begin(SERIAL_BAUD);
  proto.on_setpoints(handle_setpoints);
  proto.on_command(handle_command);
  safety.init();
  canbus.begin();
  for (uint8_t i = 0; i < MAX_MOTORS; ++i) buffers[i].init(2048);
  for (uint8_t i = 0; i < MAX_MOTORS; ++i) {
    pos_offset[i] = 0.0f;
    calib_active[i] = false;
    calib_start_us[i] = 0;
    buffer_underrun[i] = false;
    last_error_code[i] = 0;
    last_ref_pos[i] = 0.0f;
    last_ref_valid[i] = false;
  }
#if LED_GPIO >= 0
  rmt_tx_channel_config_t tx_chan_config = {};
  tx_chan_config.gpio_num = (gpio_num_t)LED_GPIO;
  tx_chan_config.clk_src = RMT_CLK_SRC_DEFAULT;
  tx_chan_config.resolution_hz = 10 * 1000 * 1000;
  tx_chan_config.mem_block_symbols = 64;
  tx_chan_config.trans_queue_depth = 1;
  tx_chan_config.flags.invert_out = 0;
  tx_chan_config.flags.with_dma = 0;
  if (rmt_new_tx_channel(&tx_chan_config, &led_channel) == ESP_OK) {
    rmt_enable(led_channel);
    if (led_strip_new_encoder(&led_encoder) == ESP_OK) {
      led_set_rgb(0, 0, 0);
    } else {
      led_encoder = nullptr;
    }
  } else {
    led_channel = nullptr;
  }
#endif
  DBG_PRINTLN("Booted");
}

static uint32_t last_tick = 0;
static uint32_t tick_period = 1000000UL / CONTROL_HZ;

void loop() {
  proto.poll();
  bool rx_ok = canbus.poll_rx();
  if (rx_ok) {
    led_pulse(LED_STATUS_R, LED_STATUS_G, LED_STATUS_B);
  }

  uint32_t now = micros32();
  if ((uint32_t)(now - last_tick) >= tick_period) {
    last_tick = now;
    if (time_aligned) {
      uint32_t traj_now = now - (uint32_t)t_offset;
      for (uint8_t idx = 0; idx < motors_used; ++idx) {
        // Enforce minimum buffer
        if (!buffers[idx].empty()) {
          Setpoint head{};
          buffers[idx].peek(0, &head);
          int32_t ahead = (int32_t)head.t_us - (int32_t)traj_now;
          if (ahead < (int32_t)MIN_BUFFER_US) {
            buffer_underrun[idx] = true;
          }
        }
        RefState ref{};
        bool interp_ok = interp.compute(buffers[idx], traj_now, &ref);
        if (!interp_ok) {
          // Hold-last behavior when buffer is empty: if we have a last reference, reuse it
          if (last_ref_valid[idx]) {
            ref.pos = last_ref_pos[idx];
            ref.vel = 0.0f;
            interp_ok = true; // treat as valid to avoid empty error
          } else {
            led_pulse(LED_FAIL_R, LED_FAIL_G, LED_FAIL_B);
          }
        }
        // Calibration waveform overrides
        if (calib_active[idx]) {
          uint32_t elapsed = now - calib_start_us[idx];
          const uint32_t duration = 3000000UL;  // 3 s
          if (elapsed < duration) {
            float t = elapsed * 1e-6f;
            float amp = 0.2f; // rad
            float freq = 1.0f; // Hz
            ref.pos = amp * sinf(2 * PI * freq * t);
            ref.vel = amp * 2 * PI * freq * cosf(2 * PI * freq * t);
          } else {
            calib_active[idx] = false;
          }
        }
        ref.pos = safety.clamp_pos(idx, ref.pos);
        float kp = 30.0f, kd = 0.5f, v = ref.vel;
        bool wd = safety.check_watchdog(idx, traj_now);
        if (safety.st.estop[idx] || wd || buffer_underrun[idx]) {
          safety.apply_stop(&kp, &kd, &v);
        }
        // Apply position offset
        float pos_cmd = ref.pos - pos_offset[idx];
        bool can_ok = canbus.send_cmd(motor_ids[idx] ? motor_ids[idx] : 1, pos_cmd, v, kp, kd, 0.0f);
        if (!can_ok) {
          led_pulse(LED_FAIL_R, LED_FAIL_G, LED_FAIL_B);
        }
        // Update last commanded reference for hold-last behavior
        last_ref_pos[idx] = ref.pos;
        last_ref_valid[idx] = true;
        uint16_t err = 0;
        if (!interp_ok) {
          err = ERR_INTERP_EMPTY;
        } else if (!can_ok) {
          err = ERR_CAN_TX_FAILED;
        } else if (wd) {
          err = ERR_WATCHDOG_TIMEOUT;
        } else if (buffer_underrun[idx]) {
          err = ERR_BUFFER_UNDERRUN;
        }
        if (err != last_error_code[idx]) {
          last_error_code[idx] = err;
          serial_send_error(motor_ids[idx] ? motor_ids[idx] : 1, err);
        }
        // Remove past points to keep buffer fresh
        while (!buffers[idx].empty()) {
          Setpoint sp{};
          buffers[idx].peek(0, &sp);
          if (sp.t_us + 2000 < traj_now) {
            buffers[idx].pop(&sp);
          } else {
            break;
          }
        }
      }
    }
  }
#if LED_GPIO >= 0
  if (led_pulse_until_us != 0 && (int64_t)now >= led_pulse_until_us) {
    led_set_rgb(0, 0, 0);
    led_pulse_until_us = 0;
  }
#endif
  // Telemetry at ~10 Hz
  static uint32_t last_telem = 0;
  if ((uint32_t)(now - last_telem) >= 100000) {
    last_telem = now;
    for (uint8_t i = 0; i < motors_used; ++i) {
      uint16_t status = 0;
      if (safety.st.estop[i]) status |= 1;
      if (calib_active[i]) status |= 2;
      if (safety.st.wd_tripped[i]) status |= 4;
      if (buffer_underrun[i]) status |= 8;
      serial_send_telemetry(motor_ids[i] ? motor_ids[i] : 1, proto.stats.frames_ok, canbus.can_rx_flags, canbus.last_can_id, status);
    }
  }
}

extern "C" void app_main(void) {
  setup();
  for (;;) {
    loop();
    vTaskDelay(1);
  }
}
