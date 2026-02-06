#include <cmath>
#include <cstdlib>

#include "esp_attr.h"
#include "nvs.h"
#include "nvs_flash.h"
#include "sys/cdefs.h"
#include "esp_err.h"
#include "driver/gpio.h"
#include "driver/rmt_encoder.h"
#include "driver/rmt_tx.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#ifndef RMT_ENCODER_FUNC_ATTR
#define RMT_ENCODER_FUNC_ATTR
#endif

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

// Time alignment
static bool time_aligned = false;
static int64_t t_offset = 0;  // local_us - traj_us

static inline uint32_t micros32() { return (uint32_t)micros(); }

static inline bool limit_gpio_valid(int gpio) { return gpio >= 0; }

static volatile bool limit_min_active_isr = false;
static volatile bool limit_max_active_isr = false;
static volatile bool limit_any_active_isr = false;
static volatile bool limit_led_override = false;
static bool error_led_override = false;

static uint8_t led_prev_r = 0;
static uint8_t led_prev_g = 0;
static uint8_t led_prev_b = 0;

static bool error_led_active = false;
static bool homing_active = false;
static bool serial_started = false;
RTC_DATA_ATTR static float homing_pos_cmd_rtc = 0.0f;
static bool homing_can_enabled = false;
static float homing_zero_pos_rad = 0.0f;
static float last_cmd_pos_rad = 0.0f;
static uint32_t nvs_last_save_us = 0;
// serial_pause blinking handled by led_update

enum LedMode : uint8_t {
  LED_MODE_IDLE = 0,
  LED_MODE_STREAMING,
  LED_MODE_HOLD_LAST,
  LED_MODE_SERIAL_PAUSE,
  LED_MODE_CALIB,
  LED_MODE_HOMING,
  LED_MODE_INTERP_EMPTY,
  LED_MODE_CAN_TX_FAIL,
  LED_MODE_BUFFER_UNDERRUN,
  LED_MODE_WATCHDOG,
  LED_MODE_ESTOP,
  LED_MODE_LIMIT,
};

static LedMode led_mode = LED_MODE_IDLE;

enum HomingState : uint8_t {
  HOMING_IDLE = 0,
  HOMING_INIT,
  HOMING_WAIT_POS,
  HOMING_SEEK_MAX,
  HOMING_STOP_MAX,
  HOMING_BACKOFF_MAX,
  HOMING_SEEK_MIN,
  HOMING_STOP_MIN,
  HOMING_BACKOFF_MIN,
  HOMING_CLEAR_MIN,
  HOMING_GO_CENTER,
  HOMING_DONE,
  HOMING_FAIL,
};

static HomingState homing_state = HOMING_IDLE;
static uint32_t homing_state_start_us = 0;
static uint32_t homing_last_cmd_us = 0;
static float homing_pos_cmd = 0.0f;
static uint32_t homing_max_hit_us = 0;
static uint32_t homing_min_hit_us = 0;
static uint32_t limit_resync_last_us = 0;
static uint32_t homing_start_us = 0;
static float homing_max_pos_rad = 0.0f;
static float homing_min_pos_rad = 0.0f;
static float homing_center_pos_rad = 0.0f;
static float homing_low_rad = 0.0f;
static float homing_high_rad = 0.0f;
static float rad_per_mm = 0.0f;
static bool homing_calibrated = false;
static bool homing_use_vel_mode = (HOMING_USE_VEL_MODE != 0);
static bool homing_allow_pos_fallback = false;
static bool homing_vel_warmup_done = false;
static bool serial_pause_active = false;
static bool hold_last_active = false;
static bool hold_last_enabled = true;
static float last_ref_pos = 0.0f;
static bool last_ref_valid = false;

static bool limit_min_raw = false;
static bool limit_max_raw = false;
static bool limit_min_stable = false;
static bool limit_max_stable = false;
static uint32_t limit_min_change_us = 0;
static uint32_t limit_max_change_us = 0;
static volatile bool limit_min_rise_isr = false;
static volatile bool limit_max_rise_isr = false;

static bool serial_link_ok() {
  if (!serial_started) return false;
  if (proto.last_rx_us == 0) return false;
  uint32_t now = micros32();
  return (uint32_t)(now - proto.last_rx_us) <= (SERIAL_LINK_TIMEOUT_MS * 1000UL);
}

static bool allow_motion(bool homing_phase) {
  if (safety.st.estop[0]) return false;
  if (limit_any_active_isr && !homing_phase) return false;
  if (!homing_phase && !homing_calibrated) return false;
  if (!homing_phase && !hold_last_active && (!serial_link_ok() || serial_pause_active)) return false;
  return true;
}

static void limit_backoff_step() {
  if (!limit_any_active_isr || homing_active) return;
  bool min_active = limit_min_active_isr;
  bool max_active = limit_max_active_isr;
  float v = 0.0f;
  if (min_active && !max_active) {
    v = HOMING_BACKOFF_VEL;
  } else if (max_active && !min_active) {
    v = -HOMING_BACKOFF_VEL;
  } else {
    canbus.send_stop(motor_id);
    return;
  }
  canbus.send_enable(motor_id);
  canbus.send_vel(motor_id, v);
}

static void led_set_rgb(uint8_t r, uint8_t g, uint8_t b);
static void led_set_base(uint8_t r, uint8_t g, uint8_t b);

static void nvs_load_last_pos() {
  esp_err_t err = nvs_flash_init();
  if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
    nvs_flash_erase();
    nvs_flash_init();
  }
  nvs_handle_t handle;
  if (nvs_open("robstride", NVS_READONLY, &handle) == ESP_OK) {
    float v = 0.0f;
    if (nvs_get_blob(handle, "last_pos", &v, nullptr) == ESP_OK) {
      homing_pos_cmd_rtc = v;
    }
    nvs_close(handle);
  }
}

static void nvs_save_last_pos(float v) {
  uint32_t now = micros32();
  if ((now - nvs_last_save_us) < 500000) return;
  nvs_last_save_us = now;
  nvs_handle_t handle;
  if (nvs_open("robstride", NVS_READWRITE, &handle) == ESP_OK) {
    nvs_set_blob(handle, "last_pos", &v, sizeof(v));
    nvs_commit(handle);
    nvs_close(handle);
  }
}

static void IRAM_ATTR limit_isr_handler(void* arg) {
  int gpio = (int)(intptr_t)arg;
  int level = gpio_get_level((gpio_num_t)gpio);
  bool active = (level == LIMIT_SW_ACTIVE_LEVEL);
  if (gpio == LIMIT_SW_MIN_GPIO) {
    limit_min_active_isr = active;
    if (active) limit_min_rise_isr = true;
  } else if (gpio == LIMIT_SW_MAX_GPIO) {
    limit_max_active_isr = active;
    if (active) limit_max_rise_isr = true;
  }
  limit_any_active_isr = (limit_min_active_isr || limit_max_active_isr);
  limit_led_override = limit_any_active_isr;
}

static void limit_switch_resync() {
  if (!limit_gpio_valid(LIMIT_SW_MIN_GPIO) && !limit_gpio_valid(LIMIT_SW_MAX_GPIO)) return;
  uint32_t now = micros32();
  if ((now - limit_resync_last_us) < 20000) return;
  limit_resync_last_us = now;
  if (limit_gpio_valid(LIMIT_SW_MIN_GPIO)) {
    bool raw = (gpio_get_level((gpio_num_t)LIMIT_SW_MIN_GPIO) == LIMIT_SW_ACTIVE_LEVEL);
    if (raw != limit_min_raw) {
      limit_min_raw = raw;
      limit_min_change_us = now;
    }
    if ((now - limit_min_change_us) >= (LIMIT_SW_DEBOUNCE_MS * 1000UL)) {
      bool prev = limit_min_stable;
      limit_min_stable = limit_min_raw;
      if (limit_min_stable && !prev) limit_min_rise_isr = true;
      limit_min_active_isr = limit_min_stable;
    }
  }
  if (limit_gpio_valid(LIMIT_SW_MAX_GPIO)) {
    bool raw = (gpio_get_level((gpio_num_t)LIMIT_SW_MAX_GPIO) == LIMIT_SW_ACTIVE_LEVEL);
    if (raw != limit_max_raw) {
      limit_max_raw = raw;
      limit_max_change_us = now;
    }
    if ((now - limit_max_change_us) >= (LIMIT_SW_DEBOUNCE_MS * 1000UL)) {
      bool prev = limit_max_stable;
      limit_max_stable = limit_max_raw;
      if (limit_max_stable && !prev) limit_max_rise_isr = true;
      limit_max_active_isr = limit_max_stable;
    }
  }
  limit_any_active_isr = (limit_min_active_isr || limit_max_active_isr);
  limit_led_override = limit_any_active_isr;
}

static void limit_switch_init() {
  gpio_config_t io = {};
  io.intr_type = GPIO_INTR_POSEDGE;
  io.mode = GPIO_MODE_INPUT;
  io.pull_up_en = GPIO_PULLUP_ENABLE;
  io.pull_down_en = GPIO_PULLDOWN_DISABLE;
  io.pin_bit_mask = 0;
  if (limit_gpio_valid(LIMIT_SW_MIN_GPIO)) {
    io.pin_bit_mask |= (1ULL << LIMIT_SW_MIN_GPIO);
  }
  if (limit_gpio_valid(LIMIT_SW_MAX_GPIO)) {
    io.pin_bit_mask |= (1ULL << LIMIT_SW_MAX_GPIO);
  }
  if (io.pin_bit_mask != 0) {
    gpio_config(&io);
    gpio_install_isr_service(0);
    if (limit_gpio_valid(LIMIT_SW_MIN_GPIO)) {
      limit_min_active_isr = (gpio_get_level((gpio_num_t)LIMIT_SW_MIN_GPIO) == LIMIT_SW_ACTIVE_LEVEL);
      limit_min_raw = limit_min_active_isr;
      limit_min_stable = limit_min_active_isr;
      limit_min_change_us = micros32();
      limit_min_rise_isr = false;
      gpio_isr_handler_add((gpio_num_t)LIMIT_SW_MIN_GPIO, limit_isr_handler, (void*)(intptr_t)LIMIT_SW_MIN_GPIO);
    }
    if (limit_gpio_valid(LIMIT_SW_MAX_GPIO)) {
      limit_max_active_isr = (gpio_get_level((gpio_num_t)LIMIT_SW_MAX_GPIO) == LIMIT_SW_ACTIVE_LEVEL);
      limit_max_raw = limit_max_active_isr;
      limit_max_stable = limit_max_active_isr;
      limit_max_change_us = micros32();
      limit_max_rise_isr = false;
      gpio_isr_handler_add((gpio_num_t)LIMIT_SW_MAX_GPIO, limit_isr_handler, (void*)(intptr_t)LIMIT_SW_MAX_GPIO);
    }
    limit_any_active_isr = (limit_min_active_isr || limit_max_active_isr);
  }
}

static inline bool limit_min_active() {
  if (!limit_gpio_valid(LIMIT_SW_MIN_GPIO)) return false;
  return limit_min_stable;
}

static inline bool limit_max_active() {
  if (!limit_gpio_valid(LIMIT_SW_MAX_GPIO)) return false;
  return limit_max_stable;
}

static void homing_led_breathe() {
  static uint32_t last_us = 0;
  uint32_t now = micros32();
  if ((now - last_us) < 30000) return;  // ~33 Hz update
  last_us = now;
  float phase = (now % 2000000UL) * (2.0f * PI / 2000000.0f);  // 2 s period
  float s = (sinf(phase) + 1.0f) * 0.5f;
  uint8_t b = (uint8_t)(10 + (s * 70.0f));
  led_set_base(0, 0, b);
}

static void homing_enter(HomingState state) {
  homing_state = state;
  homing_state_start_us = micros32();
  homing_last_cmd_us = 0;
}

static void homing_send_motion(float v) {
  if (!allow_motion(true)) {
    canbus.send_stop(motor_id);
    return;
  }
  uint32_t now = micros32();
  float ramp = 1.0f;
  if (homing_start_us != 0) {
    float elapsed_s = (now - homing_start_us) * 1e-6f;
    float ramp_s = HOMING_RAMP_MS * 1e-3f;
    if (ramp_s > 0.0f && elapsed_s < ramp_s) {
      ramp = elapsed_s / ramp_s;
    }
  }
  v *= ramp;
  if (homing_last_cmd_us == 0) {
    homing_last_cmd_us = now;
    return;
  }
  if ((now - homing_last_cmd_us) < (HOMING_STEP_MS * 1000UL)) return;
  float dt = (now - homing_last_cmd_us) * 1e-6f;
  homing_last_cmd_us = now;
  homing_pos_cmd += v * dt;
  homing_pos_cmd_rtc = homing_pos_cmd;
  if (homing_use_vel_mode) {
    canbus.send_vel(motor_id, v);
  } else if (homing_allow_pos_fallback) {
    canbus.send_cmd(motor_id, homing_pos_cmd, 0.0f, HOMING_KP, HOMING_KD, 0.0f);
  }
  nvs_save_last_pos(homing_pos_cmd_rtc);
}

static void homing_step() {
  if (!homing_active) return;
  if (!homing_use_vel_mode) {
    homing_allow_pos_fallback = true;
  }
  if (!limit_gpio_valid(LIMIT_SW_MIN_GPIO) || !limit_gpio_valid(LIMIT_SW_MAX_GPIO)) {
    homing_active = false;
    homing_state = HOMING_DONE;
    return;
  }
  if (!homing_can_enabled) {
    canbus.send_stop(motor_id);
    if (ROBSTRIDE_ENABLE_POS_READ && canbus.last_pos_valid) {
      canbus.send_cmd(motor_id, canbus.last_pos_rad, 0.0f, HOMING_KP, HOMING_KD, 0.0f);
    }
    homing_can_enabled = true;
    homing_vel_warmup_done = false;
    homing_last_cmd_us = 0;
  }
  homing_led_breathe();
  uint32_t now = micros32();
  uint32_t elapsed = now - homing_state_start_us;
  if (homing_use_vel_mode && elapsed >= (HOMING_VEL_FALLBACK_MS * 1000UL)) {
    homing_use_vel_mode = false;
    homing_allow_pos_fallback = true;
    if (canbus.last_pos_valid) {
      homing_pos_cmd = canbus.last_pos_rad;
      homing_pos_cmd_rtc = homing_pos_cmd;
    }
  }
  float dir_to_min = HOMING_DIR;
  float dir_to_max = -HOMING_DIR;

  if (elapsed >= (HOMING_TIMEOUT_MS * 1000UL)) {
    homing_state = HOMING_FAIL;
  }

  switch (homing_state) {
    case HOMING_INIT:
      if (limit_min_active() && limit_max_active()) {
        homing_state = HOMING_FAIL;
        break;
      }
      homing_pos_cmd = homing_pos_cmd_rtc;
      homing_max_hit_us = 0;
      homing_min_hit_us = 0;
      homing_zero_pos_rad = 0.0f;
      homing_allow_pos_fallback = false;
      if (ROBSTRIDE_ENABLE_POS_READ) {
        canbus.last_pos_valid = false;
        canbus.request_pos(motor_id);
        homing_enter(HOMING_WAIT_POS);
      } else if (limit_max_active()) {
        homing_enter(HOMING_BACKOFF_MAX);
      } else {
        homing_enter(HOMING_SEEK_MAX);
      }
      break;
    case HOMING_WAIT_POS:
      if (canbus.last_pos_valid) {
        homing_pos_cmd = canbus.last_pos_rad;
        homing_pos_cmd_rtc = homing_pos_cmd;
      }
      if (canbus.last_pos_valid ||
          (elapsed >= (ROBSTRIDE_POS_READ_TIMEOUT_MS * 1000UL))) {
        if (limit_max_active()) {
          homing_enter(HOMING_BACKOFF_MAX);
        } else {
          homing_enter(HOMING_SEEK_MAX);
        }
      }
      break;
    case HOMING_BACKOFF_MAX:
      if (!homing_vel_warmup_done) {
        if (homing_last_cmd_us == 0) homing_last_cmd_us = now;
        if ((now - homing_last_cmd_us) >= (HOMING_VEL_WARMUP_MS * 1000UL)) {
          homing_vel_warmup_done = true;
          canbus.send_enable(motor_id);
        } else {
          canbus.send_vel(motor_id, 0.0f);
          break;
        }
      }
      if (!limit_max_active()) {
        homing_enter(HOMING_SEEK_MIN);
        break;
      }
      homing_send_motion(dir_to_min * HOMING_BACKOFF_SLOW_VEL);
      break;
    case HOMING_SEEK_MAX:
      if (!homing_vel_warmup_done) {
        if (homing_last_cmd_us == 0) homing_last_cmd_us = now;
        if ((now - homing_last_cmd_us) >= (HOMING_VEL_WARMUP_MS * 1000UL)) {
          homing_vel_warmup_done = true;
          canbus.send_enable(motor_id);
        } else {
          canbus.send_vel(motor_id, 0.0f);
          break;
        }
      }
      if (limit_max_active() || limit_max_rise_isr) {
        limit_max_rise_isr = false;
        homing_max_hit_us = now;
        homing_max_pos_rad = homing_pos_cmd;
        homing_enter(HOMING_STOP_MAX);
        homing_last_cmd_us = 0;
        canbus.send_vel(motor_id, 0.0f);
        break;
      }
      homing_send_motion(dir_to_max * HOMING_VEL);
      break;
    case HOMING_STOP_MAX:
      if ((now - homing_state_start_us) >= (HOMING_STOP_DWELL_MS * 1000UL)) {
        homing_enter(HOMING_BACKOFF_MAX);
      }
      canbus.send_vel(motor_id, 0.0f);
      break;
    case HOMING_SEEK_MIN:
      if (!homing_vel_warmup_done) {
        if (homing_last_cmd_us == 0) homing_last_cmd_us = now;
        if ((now - homing_last_cmd_us) >= (HOMING_VEL_WARMUP_MS * 1000UL)) {
          homing_vel_warmup_done = true;
          canbus.send_enable(motor_id);
        } else {
          canbus.send_vel(motor_id, 0.0f);
          break;
        }
      }
      if (limit_min_active() || limit_min_rise_isr) {
        limit_min_rise_isr = false;
        homing_min_hit_us = now;
        homing_min_pos_rad = homing_pos_cmd;
        float travel_rad = homing_max_pos_rad - homing_min_pos_rad;
        if (travel_rad < 0.0f) travel_rad = -travel_rad;
        if (travel_rad > 1e-6f) {
          rad_per_mm = travel_rad / TOTAL_RANGE_MM;
          homing_calibrated = true;
        } else {
          homing_calibrated = false;
        }
        homing_low_rad = (homing_min_pos_rad < homing_max_pos_rad) ? homing_min_pos_rad : homing_max_pos_rad;
        homing_high_rad = (homing_min_pos_rad < homing_max_pos_rad) ? homing_max_pos_rad : homing_min_pos_rad;
        homing_zero_pos_rad = homing_min_pos_rad + (HOMING_CLEAR_MM * rad_per_mm);
        homing_allow_pos_fallback = true;
        homing_enter(HOMING_STOP_MIN);
        homing_last_cmd_us = 0;
        canbus.send_vel(motor_id, 0.0f);
        break;
      }
      homing_send_motion(dir_to_min * HOMING_VEL);
      break;
    case HOMING_STOP_MIN:
      if ((now - homing_state_start_us) >= (HOMING_STOP_DWELL_MS * 1000UL)) {
        homing_enter(HOMING_BACKOFF_MIN);
      }
      canbus.send_vel(motor_id, 0.0f);
      break;
    case HOMING_BACKOFF_MIN:
      if (!limit_min_active()) {
        homing_enter(HOMING_CLEAR_MIN);
        break;
      }
      homing_send_motion(dir_to_max * HOMING_BACKOFF_SLOW_VEL);
      break;
    case HOMING_CLEAR_MIN: {
      if (!homing_vel_warmup_done) {
        if (homing_last_cmd_us == 0) homing_last_cmd_us = now;
        if ((now - homing_last_cmd_us) >= (HOMING_VEL_WARMUP_MS * 1000UL)) {
          homing_vel_warmup_done = true;
          canbus.send_enable(motor_id);
        } else {
          canbus.send_vel(motor_id, 0.0f);
          break;
        }
      }
      float clear_target = homing_zero_pos_rad;
      if (!limit_min_active() && homing_pos_cmd >= clear_target) {
        homing_center_pos_rad = homing_zero_pos_rad + (TOTAL_RANGE_MM * 0.5f * rad_per_mm);
        homing_zero_pos_rad = homing_center_pos_rad;
        homing_enter(HOMING_GO_CENTER);
        break;
      }
      homing_send_motion(dir_to_max * HOMING_BACKOFF_VEL);
      break;
    }
    case HOMING_GO_CENTER: {
      if (!homing_vel_warmup_done) {
        if (homing_last_cmd_us == 0) homing_last_cmd_us = now;
        if ((now - homing_last_cmd_us) >= (HOMING_VEL_WARMUP_MS * 1000UL)) {
          homing_vel_warmup_done = true;
          canbus.send_enable(motor_id);
        } else {
          canbus.send_vel(motor_id, 0.0f);
          break;
        }
      }
      float err = homing_center_pos_rad - homing_pos_cmd;
      float tol = HOMING_CENTER_TOL_MM * rad_per_mm;
      if (err < 0.0f) err = -err;
      if (err <= tol) {
        homing_state = HOMING_DONE;
        break;
      }
      float dir = (homing_center_pos_rad >= homing_pos_cmd) ? 1.0f : -1.0f;
      homing_send_motion(dir * (2.0f * HOMING_VEL));
      break;
    }
    case HOMING_DONE: {
      homing_pos_cmd = homing_center_pos_rad;
      pos_offset = 0.0f;
      time_aligned = false;
      homing_active = false;
      homing_can_enabled = false;
      homing_allow_pos_fallback = false;
      led_set_base(0, 0, 0);
      break;
    }
    case HOMING_FAIL:
      homing_active = false;
      homing_can_enabled = false;
      homing_allow_pos_fallback = false;
      error_led_active = true;
      safety.set_estop(0, true);
      led_set_rgb(0, 0, 0);
      break;
    default:
      break;
  }
}

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

static void led_set_base(uint8_t r, uint8_t g, uint8_t b) {
  led_prev_r = r;
  led_prev_g = g;
  led_prev_b = b;
  if (!limit_led_override && !error_led_override) {
    led_set_rgb(r, g, b);
  }
}

static void led_pulse(uint8_t r, uint8_t g, uint8_t b) {
#if LED_GPIO >= 0
  if (led_mode != LED_MODE_IDLE && led_mode != LED_MODE_STREAMING) return;
  uint32_t now = (uint32_t)micros();
  led_pulse_until_us = (int64_t)now + LED_PULSE_US;
  led_set_base(r, g, b);
#endif
}

static void led_update(uint32_t now_us) {
#if LED_GPIO >= 0
  auto blink = [&](uint32_t period_us, uint8_t r, uint8_t g, uint8_t b) {
    uint32_t phase = now_us % period_us;
    if (phase < period_us / 2) led_set_rgb(r, g, b);
    else led_set_rgb(0, 0, 0);
  };
  auto double_blink = [&](uint32_t period_us, uint8_t r, uint8_t g, uint8_t b) {
    uint32_t phase = now_us % period_us;
    if (phase < 60000 || (phase >= 120000 && phase < 180000)) led_set_rgb(r, g, b);
    else led_set_rgb(0, 0, 0);
  };
  auto breathe = [&](uint32_t period_us, uint8_t r, uint8_t g, uint8_t b) {
    float phase = (now_us % period_us) * (2.0f * PI / (float)period_us);
    float s = (sinf(phase) + 1.0f) * 0.5f;
    led_set_rgb((uint8_t)(r * s), (uint8_t)(g * s), (uint8_t)(b * s));
  };

  switch (led_mode) {
    case LED_MODE_LIMIT:
      led_set_rgb(0, 64, 0); // solid green
      break;
    case LED_MODE_ESTOP:
      led_set_rgb(64, 0, 0); // solid red
      break;
    case LED_MODE_WATCHDOG:
      blink(200000, 64, 0, 0); // fast red blink
      break;
    case LED_MODE_BUFFER_UNDERRUN:
      double_blink(600000, 64, 0, 0); // red double blink
      break;
    case LED_MODE_CAN_TX_FAIL:
      blink(200000, 64, 0, 64); // fast magenta blink
      break;
    case LED_MODE_INTERP_EMPTY:
      blink(600000, 64, 0, 64); // slow magenta blink
      break;
    case LED_MODE_SERIAL_PAUSE:
      blink(600000, 64, 32, 0); // slow amber blink
      break;
    case LED_MODE_HOLD_LAST:
      blink(600000, 0, 64, 64); // slow cyan blink
      break;
    case LED_MODE_CALIB:
      breathe(2000000, 64, 0, 64); // purple breathe
      break;
    case LED_MODE_HOMING:
      breathe(2000000, 0, 0, 64); // blue breathe
      break;
    case LED_MODE_STREAMING:
      blink(1000000, 32, 32, 32); // slow white blink
      break;
    case LED_MODE_IDLE:
    default:
      led_set_rgb(0, 0, 0);
      break;
  }
#endif
}

static void handle_setpoints(uint32_t ts_us, const Setpoint* sps, uint8_t count) {
  if (!homing_calibrated) {
#if !ALLOW_SETPOINTS_WITHOUT_HOMING
    return;
#endif
  }
  if (serial_pause_active) {
    serial_pause_active = false;
    error_led_active = false;
  }
  // Align time on first packet
  uint32_t now_us = micros32();
  if (!time_aligned) {
    t_offset = (int64_t)now_us - (int64_t)ts_us;
    time_aligned = true;
  }
  // Push into buffer (single motor firmware uses DEFAULT_MOTOR_ID)
  for (uint8_t i = 0; i < count; ++i) {
    Setpoint sp = sps[i];
    // Input position/velocity are in millimeters; convert to radians after homing.
    float half_range = TOTAL_RANGE_MM * 0.5f;
    if (sp.pos < -half_range) sp.pos = -half_range;
    if (sp.pos > half_range) sp.pos = half_range;
    float pos_rad = homing_zero_pos_rad + sp.pos * rad_per_mm;
    float vel_rad = sp.vel * rad_per_mm;
    if (homing_calibrated) {
      if (pos_rad < homing_low_rad) pos_rad = homing_low_rad;
      if (pos_rad > homing_high_rad) pos_rad = homing_high_rad;
    }
    sp.pos = pos_rad;
    sp.vel = vel_rad;
    last_ref_pos = sp.pos;
    last_ref_valid = true;
    if (!safety.within_limits(sp.pos)) {
      continue;
    }
    buffer.push(sp);
    safety.update_last_sp(0, sp.t_us);
  }
  last_error_code = 0;
  error_led_active = false;
}

static void handle_command(uint8_t cmd) {
  led_pulse(LED_CMD_R, LED_CMD_G, LED_CMD_B);
  switch (cmd) {
    case 1:  // enable
      // Clear ESTOP and underrun flags, then enable motor
      safety.set_estop(0, false);
      buffer_underrun = false;
      canbus.send_enable(::motor_id);
      break;
    case 2:  // disable
      // Not a defined disable in RobStride docs provided. Enter safe stop for selected motors.
      safety.set_estop(0, true);
      break;
    case 3:  // stop
      safety.set_estop(0, true);
      break;
    case 4:  // zero offset (software only placeholder)
      {
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
      // Force homing sequence
      canbus.send_stop(::motor_id);
      buffer.clear();
      safety.set_estop(0, false);
      error_led_active = false;
      homing_active = true;
      homing_use_vel_mode = (HOMING_USE_VEL_MODE != 0);
      homing_allow_pos_fallback = false;
      homing_vel_warmup_done = false;
      homing_can_enabled = false;
      homing_start_us = micros32();
      homing_enter(HOMING_INIT);
      homing_calibrated = false;
      time_aligned = false;
      buffer_underrun = false;
      break;
    case 7:  // calibrate
      calib_active = true;
      calib_start_us = micros32();
      break;
  }
}

void setup() {
  nvs_load_last_pos();
  canbus.begin();
  limit_switch_init();
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
  proto.on_setpoints(handle_setpoints);
  proto.on_command(handle_command);
#if STATIC_CALIB_ENABLE
  // Static calibration: accept setpoints immediately
  rad_per_mm = STATIC_RAD_PER_MM;
  homing_zero_pos_rad = STATIC_ZERO_POS_RAD;
  homing_calibrated = (rad_per_mm > 0.0f);
  homing_active = false;
  homing_state = HOMING_DONE;
#else
  homing_active = true;
  homing_can_enabled = false;
  homing_start_us = micros32();
  homing_enter(HOMING_INIT);
#endif
  DBG_PRINTLN("Booted");
}

static uint32_t last_tick = 0;
static uint32_t tick_period = 1000000UL / CONTROL_HZ;

void loop() {
  limit_switch_resync();
  hold_last_active = buffer.empty() && last_ref_valid;
  if (limit_any_active_isr && !homing_active) {
    limit_backoff_step();
  }
  homing_step();
  #if SERIAL_START_EARLY
  if (!serial_started) {
    proto.begin(SERIAL_BAUD);
    serial_started = true;
  }
  #else
  if (!serial_started && (homing_state == HOMING_DONE || homing_state == HOMING_FAIL)) {
    proto.begin(SERIAL_BAUD);
    serial_started = true;
    if (homing_state == HOMING_FAIL) {
      led_pulse(LED_FAIL_R, LED_FAIL_G, LED_FAIL_B);
    }
  }
  #endif
  if (serial_started) {
    proto.poll();
    if (proto.last_rx_us != 0) {
      uint32_t now_us = micros32();
      if ((uint32_t)(now_us - proto.last_rx_us) > (SERIAL_LINK_TIMEOUT_MS * 1000UL)) {
        if (!hold_last_active) {
          serial_pause_active = true;
          error_led_active = true;
        }
      }
    }
  }
  // LED state machine (priority order)
  if (limit_any_active_isr) {
    led_mode = LED_MODE_LIMIT;
  } else if (safety.st.estop[0]) {
    led_mode = LED_MODE_ESTOP;
  } else if (safety.st.wd_tripped[0]) {
    led_mode = LED_MODE_WATCHDOG;
  } else if (buffer_underrun) {
    led_mode = LED_MODE_BUFFER_UNDERRUN;
  } else if (last_error_code == ERR_CAN_TX_FAILED) {
    led_mode = LED_MODE_CAN_TX_FAIL;
  } else if (last_error_code == ERR_INTERP_EMPTY) {
    led_mode = LED_MODE_INTERP_EMPTY;
  } else if (serial_pause_active) {
    led_mode = LED_MODE_SERIAL_PAUSE;
  } else if (hold_last_active) {
    led_mode = LED_MODE_HOLD_LAST;
  } else if (calib_active) {
    led_mode = LED_MODE_CALIB;
  } else if (homing_active) {
    led_mode = LED_MODE_HOMING;
  } else if (serial_started && time_aligned) {
    led_mode = LED_MODE_STREAMING;
  } else {
    led_mode = LED_MODE_IDLE;
  }
  led_update(micros32());
  bool rx_ok = canbus.poll_rx();
  if (rx_ok) {
    led_pulse(LED_STATUS_R, LED_STATUS_G, LED_STATUS_B);
  }

  uint32_t now = micros32();
  if ((uint32_t)(now - last_tick) >= tick_period) {
    last_tick = now;
    if (serial_started && time_aligned) {
      uint32_t traj_now = now - (uint32_t)t_offset;
      if (limit_any_active_isr && !homing_active) {
        limit_backoff_step();
        goto next_cycle;
      }
      // Enforce minimum buffer
      if (!buffer.empty()) {
        Setpoint head{};
        buffer.peek(0, &head);
        int32_t ahead = (int32_t)head.t_us - (int32_t)traj_now;
        buffer_underrun = (ahead < (int32_t)MIN_BUFFER_US);
      }
      hold_last_active = buffer.empty() && last_ref_valid;
      if (!buffer.empty() || hold_last_active) {
        safety.update_last_sp(0, traj_now);
        buffer_underrun = false;
        serial_pause_active = false;
        error_led_active = false;
      }
      if (hold_last_active) {
        // Treat last setpoint as continuously refreshed.
        // (already refreshed above)
      }
      RefState ref{};
      bool interp_ok = interp.compute(buffer, traj_now, &ref);
      if (!interp_ok) {
        if (hold_last_active) {
          ref.pos = last_ref_pos;
          ref.vel = 0.0f;
          interp_ok = true;
          buffer_underrun = false;
        } else {
          led_pulse(LED_FAIL_R, LED_FAIL_G, LED_FAIL_B);
        }
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
      bool wd = false;
      if (!hold_last_enabled) {
        if (buffer.empty() && !hold_last_active) {
          wd = safety.check_watchdog(0, traj_now);
        }
      }
      if (hold_last_enabled) {
        safety.st.wd_tripped[0] = false;
      }
      if (safety.st.estop[0] || wd || buffer_underrun) {
        safety.apply_stop(&kp, &kd, &v);
      }
      if (!allow_motion(false)) {
        canbus.send_stop(motor_id);
        goto next_cycle;
      }
      // Apply position offset
      float pos_cmd = ref.pos - pos_offset;
      if (homing_calibrated) {
        if (pos_cmd < homing_low_rad) pos_cmd = homing_low_rad;
        if (pos_cmd > homing_high_rad) pos_cmd = homing_high_rad;
      }
      bool can_ok = canbus.send_cmd(motor_id, pos_cmd, v, kp, kd, 0.0f);
      if (!can_ok) {
        led_pulse(LED_FAIL_R, LED_FAIL_G, LED_FAIL_B);
      }
      if (can_ok) {
        last_cmd_pos_rad = pos_cmd;
        nvs_save_last_pos(last_cmd_pos_rad);
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
        serial_send_error(err);
      }
      error_led_active = (err != 0);
      if (interp_ok) {
        last_ref_pos = ref.pos;
        last_ref_valid = true;
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
next_cycle:
    (void)0;
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
    if (limit_min_stable) status |= 16;
    if (limit_max_stable) status |= 32;
    serial_send_telemetry(proto.stats.frames_ok, canbus.can_rx_flags, canbus.last_can_id, status);
  }
}

extern "C" void app_main(void) {
  setup();
  for (;;) {
    loop();
    vTaskDelay(1);
  }
}
