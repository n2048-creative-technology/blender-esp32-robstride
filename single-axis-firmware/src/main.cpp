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

#ifndef PI
#define PI 3.14159265358979323846f
#endif

static SerialProtocol proto;
static RobStrideCAN canbus;
static Interpolator interp;
static Safety safety;

static RingBuffer buffer;
static uint8_t motor_id = DEFAULT_MOTOR_ID;
static float pos_offset = 0.0f;
static bool calib_active = false;
static uint32_t calib_start_us = 0;
static bool buffer_underrun = false;
static uint16_t last_error_code = 0;

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
  // Push into buffer for configured motor ID only
  for (uint8_t i = 0; i < count; ++i) {
    if (sps[i].motor_id != motor_id && sps[i].motor_id != 0) {
      continue;
    }
    Setpoint sp = sps[i];
    if (!safety.within_limits(sp.pos)) {
      continue;
    }
    buffer.push(sp);
    safety.update_last_sp(0, sp.t_us);
  }
}

static void handle_command(uint8_t cmd, uint8_t motor_id) {
  led_pulse(LED_CMD_R, LED_CMD_G, LED_CMD_B);
  switch (cmd) {
    case 1:  // enable
      if (motor_id == 0 || motor_id == ::motor_id) {
        canbus.send_enable(::motor_id);
      }
      break;
    case 2:  // disable
      // Not a defined disable in RobStride docs provided. Enter safe stop for selected motors.
      if (motor_id == 0 || motor_id == ::motor_id) {
        safety.set_estop(0, true);
      }
      break;
    case 3:  // stop
      if (motor_id == 0 || motor_id == ::motor_id) {
        safety.set_estop(0, true);
      }
      break;
    case 4:  // zero offset (software only placeholder)
      if (motor_id == 0 || motor_id == ::motor_id) {
        // Set current reference position as zero offset
        RefState ref{};
        uint32_t now = micros32();
        uint32_t traj_now = time_aligned ? (now - (uint32_t)t_offset) : 0;
        if (interp.compute(buffer, traj_now, &ref)) {
          pos_offset = ref.pos;
        } else {
          pos_offset = 0.0f;
        }
      }
      break;
    case 5:  // ping
      break;
    case 6:  // home
      if (motor_id == 0 || motor_id == ::motor_id) {
        // Software home: set current position as zero
        RefState ref{};
        uint32_t now = micros32();
        uint32_t traj_now = time_aligned ? (now - (uint32_t)t_offset) : 0;
        if (interp.compute(buffer, traj_now, &ref)) {
          pos_offset = ref.pos;
        } else {
          pos_offset = 0.0f;
        }
      }
      break;
    case 7:  // calibrate
      if (motor_id == 0 || motor_id == ::motor_id) {
        calib_active = true;
        calib_start_us = micros32();
      }
      break;
  }
}

void setup() {
  proto.begin(SERIAL_BAUD);
  proto.on_setpoints(handle_setpoints);
  proto.on_command(handle_command);
  canbus.begin();
  buffer.init(2048);
  pos_offset = 0.0f;
  calib_active = false;
  calib_start_us = 0;
  buffer_underrun = false;
  last_error_code = 0;
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
      // Enforce minimum buffer
      if (!buffer.empty()) {
        Setpoint head{};
        buffer.peek(0, &head);
        int32_t ahead = (int32_t)head.t_us - (int32_t)traj_now;
        if (ahead < (int32_t)MIN_BUFFER_US) {
          buffer_underrun = true;
        }
      }
      RefState ref{};
      bool interp_ok = interp.compute(buffer, traj_now, &ref);
      if (!interp_ok) {
        led_pulse(LED_FAIL_R, LED_FAIL_G, LED_FAIL_B);
      }
      // Calibration waveform overrides
      if (calib_active) {
        uint32_t elapsed = now - calib_start_us;
        const uint32_t duration = 3000000UL;  // 3 s
        if (elapsed < duration) {
          float t = elapsed * 1e-6f;
          float amp = 0.2f; // rad
          float freq = 1.0f; // Hz
          ref.pos = amp * sinf(2 * PI * freq * t);
          ref.vel = amp * 2 * PI * freq * cosf(2 * PI * freq * t);
        } else {
          calib_active = false;
        }
      }
      float kp = 30.0f, kd = 0.5f, v = ref.vel;
      bool wd = safety.check_watchdog(0, traj_now);
      if (safety.st.estop[0] || wd || buffer_underrun) {
        safety.apply_stop(&kp, &kd, &v);
      }
      // Apply position offset
      float pos_cmd = ref.pos - pos_offset;
      bool can_ok = canbus.send_cmd(motor_id, pos_cmd, v, kp, kd, 0.0f);
      if (!can_ok) {
        led_pulse(LED_FAIL_R, LED_FAIL_G, LED_FAIL_B);
      }
      uint16_t err = 0;
      if (!interp_ok) {
        err = ERR_INTERP_EMPTY;
      } else if (!can_ok) {
        err = ERR_CAN_TX_FAILED;
      } else if (wd) {
        err = ERR_WATCHDOG_TIMEOUT;
      } else if (buffer_underrun) {
        err = ERR_BUFFER_UNDERRUN;
      }
      if (err != last_error_code) {
        last_error_code = err;
        serial_send_error(motor_id, err);
      }
      // Remove past points to keep buffer fresh
      while (!buffer.empty()) {
        Setpoint sp{};
        buffer.peek(0, &sp);
        if (sp.t_us + 2000 < traj_now) {
          buffer.pop(&sp);
        } else {
          break;
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
    uint16_t status = 0;
    if (safety.st.estop[0]) status |= 1;
    if (calib_active) status |= 2;
    if (safety.st.wd_tripped[0]) status |= 4;
    if (buffer_underrun) status |= 8;
    serial_send_telemetry(motor_id, proto.stats.frames_ok, canbus.can_rx_flags, canbus.last_can_id, status);
  }
}

extern "C" void app_main(void) {
  setup();
  for (;;) {
    loop();
    vTaskDelay(1);
  }
}
