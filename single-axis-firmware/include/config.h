#pragma once

#include "serial_compat.h"

// Serial
#define SERIAL_BAUD 921600
// Stop motor if no valid serial frames received within this time
#define SERIAL_LINK_TIMEOUT_MS 500
// Start serial protocol immediately on boot (1) or after homing done/fail (0).
#ifndef SERIAL_START_EARLY
#define SERIAL_START_EARLY 0
#endif

// On-board addressable RGB LED (RMT-driven). Set to -1 to disable.
// ESP32-S3-DevKitC-1 onboard WS2812 is typically on GPIO48; confirm for your board.
#define LED_GPIO 48
// Pulse duration when a command is received (microseconds)
#define LED_PULSE_US 50000
// LED colors (0-255 per channel)
#define LED_CMD_R 0
#define LED_CMD_G 32
#define LED_CMD_B 32
#define LED_STATUS_R 32
#define LED_STATUS_G 32
#define LED_STATUS_B 32
#define LED_FAIL_R 64
#define LED_FAIL_G 0
#define LED_FAIL_B 64

// TWAI pins and bitrate for ESP32-C6, adjust to your wiring
// These are example GPIOs, verify against your board and transceiver
#define TWAI_TX_PIN 5
#define TWAI_RX_PIN 6
#define TWAI_BAUD 1000000

// Use RobStride CAN protocol (extended 29-bit IDs with type/host/motor layout)
#define ROBSTRIDE_MODE 1
// 16-bit host/master ID field used by RobStride extended ID layout
#define ROBSTRIDE_HOST_ID 0x00AA
// CAN identifier format: 1 = extended 29-bit, 0 = standard 11-bit.
// RobStride requires extended; leave at 1 when ROBSTRIDE_MODE is enabled.
#define CAN_USE_EXTENDED 1
// RobStride indexes
#define ROBSTRIDE_IDX_POS 0x7016
#define ROBSTRIDE_IDX_VEL 0x7017
// Use velocity mode during homing if supported by actuator firmware
#define HOMING_USE_VEL_MODE 1
// Fallback to position-based homing after this time (ms) if velocity mode is ineffective
#define HOMING_VEL_FALLBACK_MS 3000
// Homing velocity warmup before enabling (ms)
#define HOMING_VEL_WARMUP_MS 200
// Attempt to read initial position from actuator before homing
#define ROBSTRIDE_ENABLE_POS_READ 1
#define ROBSTRIDE_POS_READ_TIMEOUT_MS 200

// Motors
#define MAX_MOTORS 6
// Default RobStride motor ID for single-motor mode
#define DEFAULT_MOTOR_ID 4

// Limit switches (normally-closed to GND with internal pull-up)
// Set these GPIOs to match your wiring.
#define LIMIT_SW_MIN_GPIO 9
#define LIMIT_SW_MAX_GPIO 10
// NC switch is low when closed (normal), high when opened (limit hit).
#define LIMIT_SW_ACTIVE_LEVEL 1
// Debounce time for limit switches (ms)
#define LIMIT_SW_DEBOUNCE_MS 5

// Homing parameters
// Direction to home toward the MIN switch: -1.0f for negative, +1.0f for positive.
#define HOMING_DIR -1.0f
// Velocity used during homing (rad/s)
#define HOMING_VEL 2.0f
// Velocity used to back off if already on the switch (rad/s)
#define HOMING_BACKOFF_VEL 0.5f
// Slow backoff velocity after switch hit (rad/s)
#define HOMING_BACKOFF_SLOW_VEL 0.2f
// Dwell time to stop before reversing (ms)
#define HOMING_STOP_DWELL_MS 50
// MIT mode gains during homing
#define HOMING_KP 0.0f
#define HOMING_KD 0.5f
// Homing loop timing and timeouts
#define HOMING_STEP_MS 5
#define HOMING_TIMEOUT_MS 600000
#define HOMING_BACKOFF_TIMEOUT_MS 2000
#define HOMING_SETTLE_MS 100
// Homing ramp-up time for speed (ms)
#define HOMING_RAMP_MS 2000
// Distance to move off the MIN switch after homing (mm)
#define HOMING_CLEAR_MM 5.0f
// Centering tolerance after homing (mm)
#define HOMING_CENTER_TOL_MM 1.0f

// Motion range in millimeters after homing calibration.
#define TOTAL_RANGE_MM 1700.0f

// Optional: allow setpoints without completing homing
#ifndef ALLOW_SETPOINTS_WITHOUT_HOMING
#define ALLOW_SETPOINTS_WITHOUT_HOMING 0
#endif

// Optional: static calibration (bypass homing math)
#ifndef STATIC_CALIB_ENABLE
#define STATIC_CALIB_ENABLE 0
#endif
// If enabled, set conversion and zero offset here
#ifndef STATIC_RAD_PER_MM
#define STATIC_RAD_PER_MM 0.0f
#endif
#ifndef STATIC_ZERO_POS_RAD
#define STATIC_ZERO_POS_RAD 0.0f
#endif

// Default streaming and control rates
#define CONTROL_HZ 1000
#define MIN_BUFFER_US 100000  // 100 ms minimum buffer
#define WATCHDOG_US 100000    // 100 ms watchdog

// Safety limits (radians)
#define SOFT_LIMIT_MIN -6.283185307179586f
#define SOFT_LIMIT_MAX  6.283185307179586f

// Motion limits
#define MAX_VEL 20.0f        // rad/s
#define MAX_ACC 200.0f       // rad/s^2

// Scaling to RobStride MIT mode 8-byte frame
// The exact scaling can vary by motor firmware. Adjust as required.
// We use a typical MIT-style packing into 8 bytes with ranges defined here.
#define POS_MIN   -12.5f
#define POS_MAX    12.5f
#define VEL_MIN   -65.0f
#define VEL_MAX    65.0f
#define KP_MIN      0.0f
#define KP_MAX    500.0f
#define KD_MIN      0.0f
#define KD_MAX     10.0f
#define T_MIN     -18.0f
#define T_MAX      18.0f

// Debug
#ifdef ROBSTRIDE_DEBUG
#define DBG_PRINT(x) Serial.print(x)
#define DBG_PRINTLN(x) Serial.println(x)
#else
#define DBG_PRINT(x)
#define DBG_PRINTLN(x)
#endif
