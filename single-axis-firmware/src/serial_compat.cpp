#include "serial_compat.h"

#include "esp_err.h"

SerialCompat Serial;

void SerialCompat::begin(uint32_t baud) {
  if (!started_) {
    uart_config_t config = {};
    config.baud_rate = static_cast<int>(baud);
    config.data_bits = UART_DATA_8_BITS;
    config.parity = UART_PARITY_DISABLE;
    config.stop_bits = UART_STOP_BITS_1;
    config.flow_ctrl = UART_HW_FLOWCTRL_DISABLE;
    config.source_clk = UART_SCLK_DEFAULT;
    uart_param_config(UART_NUM_0, &config);
    uart_set_pin(UART_NUM_0, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE);
    if (!uart_is_driver_installed(UART_NUM_0)) {
      uart_driver_install(UART_NUM_0, 2048, 0, 0, nullptr, 0);
    }
    started_ = true;
  } else {
    uart_set_baudrate(UART_NUM_0, baud);
  }
}

int SerialCompat::available() {
  size_t len = 0;
  if (uart_get_buffered_data_len(UART_NUM_0, &len) != ESP_OK) return 0;
  return static_cast<int>(len);
}

int SerialCompat::read() {
  uint8_t b = 0;
  int r = uart_read_bytes(UART_NUM_0, &b, 1, 0);
  return (r == 1) ? static_cast<int>(b) : -1;
}

size_t SerialCompat::write(const uint8_t* data, size_t len) {
  if (!data || len == 0) return 0;
  int written = uart_write_bytes(UART_NUM_0, reinterpret_cast<const char*>(data), len);
  return (written < 0) ? 0U : static_cast<size_t>(written);
}

size_t SerialCompat::write(uint8_t b) {
  return write(&b, 1);
}

size_t SerialCompat::write(const char* s) {
  if (!s) return 0;
  return write(reinterpret_cast<const uint8_t*>(s), std::strlen(s));
}

void SerialCompat::print(const char* s) {
  if (s) write(s);
}

void SerialCompat::println(const char* s) {
  if (s) write(s);
  write(reinterpret_cast<const uint8_t*>("\r\n"), 2);
}

void SerialCompat::println() {
  write(reinterpret_cast<const uint8_t*>("\r\n"), 2);
}
