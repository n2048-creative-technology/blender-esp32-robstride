#include <Arduino.h>
#include "include/config.h"
#include "protocol_serial.h"
#include "ring_buffer.h"
#include "interp.h"
#include "robstride_can.h"
#include "safety.h"

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
    if (!safety.within_limits(sp.pos)) {
      continue;
    }
    buffers[idx].push(sp);
    safety.update_last_sp(idx, sp.t_us);
  }
}

static void handle_command(uint8_t cmd, uint8_t motor_id) {
  switch (cmd) {
    case 1:  // enable
      canbus.send_enable(motor_id);
      break;
    case 2:  // disable
      // Not a defined disable in RobStride docs provided. Enter safe stop for selected motors.
      for (uint8_t i = 0; i < motors_used; ++i) {
        if (motor_id == 0 || motor_ids[i] == motor_id) safety.set_estop(i, true);
      }
      break;
    case 3:  // stop
      for (uint8_t i = 0; i < motors_used; ++i) {
        if (motor_id == 0 || motor_ids[i] == motor_id) safety.set_estop(i, true);
      }
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
  canbus.begin();
  for (uint8_t i = 0; i < MAX_MOTORS; ++i) buffers[i].init(2048);
  for (uint8_t i = 0; i < MAX_MOTORS; ++i) { pos_offset[i] = 0.0f; calib_active[i] = false; calib_start_us[i] = 0; buffer_underrun[i] = false; }
  DBG_PRINTLN("Booted");
}

static uint32_t last_tick = 0;
static uint32_t tick_period = 1000000UL / CONTROL_HZ;

void loop() {
  proto.poll();
  canbus.poll_rx();

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
        bool ok = interp.compute(buffers[idx], traj_now, &ref);
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
        float kp = 30.0f, kd = 0.5f, v = ref.vel;
        bool wd = safety.check_watchdog(idx, traj_now);
        if (safety.st.estop[idx] || wd || buffer_underrun[idx]) {
          safety.apply_stop(&kp, &kd, &v);
        }
        // Apply position offset
        float pos_cmd = ref.pos - pos_offset[idx];
        canbus.send_cmd(motor_ids[idx] ? motor_ids[idx] : 1, pos_cmd, v, kp, kd, 0.0f);
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
