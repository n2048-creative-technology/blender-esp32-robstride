#pragma once

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <type_traits>

#include "driver/uart.h"
#if CONFIG_SOC_USB_SERIAL_JTAG_SUPPORTED
#include "driver/usb_serial_jtag.h"
#endif
#include "esp_timer.h"

class SerialCompat {
 public:
  void begin(uint32_t baud);
  int available();
  int read();
  size_t write(const uint8_t* data, size_t len);
  size_t write(uint8_t b);
  size_t write(const char* s);

  void print(const char* s);
  void println(const char* s);
  void println();

  template <typename T, typename std::enable_if<std::is_arithmetic<T>::value, int>::type = 0>
  void print(T v) {
    char buf[64];
    int n = 0;
    if (std::is_floating_point<T>::value) {
      n = std::snprintf(buf, sizeof(buf), "%.6f", static_cast<double>(v));
    } else {
      n = std::snprintf(buf, sizeof(buf), "%lld", static_cast<long long>(v));
    }
    if (n > 0) {
      write(reinterpret_cast<const uint8_t*>(buf), static_cast<size_t>(n));
    }
  }

  template <typename T, typename std::enable_if<std::is_arithmetic<T>::value, int>::type = 0>
  void println(T v) {
    print(v);
    println();
  }

 private:
  bool started_ = false;
  int peeked_ = -1;  // for USB-JTAG non-blocking read
  bool use_usb_ = true;    // default to USB Serial/JTAG when available
  bool usb_ready_ = false;
};

extern SerialCompat Serial;

static inline uint64_t micros() {
  return static_cast<uint64_t>(esp_timer_get_time());
}
