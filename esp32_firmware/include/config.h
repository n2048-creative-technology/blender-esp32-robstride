#pragma once

#include "serial_compat.h"

// Serial
#define SERIAL_BAUD 921600

// On-board addressable RGB LED (RMT-driven). Set to -1 to disable.
#define LED_GPIO 8
// Pulse duration when a command is received (microseconds)
#define LED_PULSE_US 50000
// LED colors (0-255 per channel)
#define LED_CMD_R 0
#define LED_CMD_G 64
#define LED_CMD_B 0
#define LED_STATUS_R 32
#define LED_STATUS_G 32
#define LED_STATUS_B 32
#define LED_FAIL_R 64
#define LED_FAIL_G 0
#define LED_FAIL_B 0

// TWAI pins and bitrate for ESP32-C6, adjust to your wiring
// These are example GPIOs, verify against your board and transceiver
#define TWAI_TX_PIN 5
#define TWAI_RX_PIN 6
#define TWAI_BAUD 1000000

// Motors
#define MAX_MOTORS 6

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
