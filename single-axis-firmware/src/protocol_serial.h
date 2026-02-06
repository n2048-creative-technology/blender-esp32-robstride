#pragma once

#include <cstddef>
#include <cstdint>

#include "include/config.h"
#include "ring_buffer.h"

enum MsgType : uint8_t {
  MSG_SETPOINTS = 1,
  MSG_COMMAND = 2,
  MSG_TELEMETRY = 3,
  MSG_ERROR = 4,
};

struct RxStats {
  uint32_t frames_ok = 0;
  uint32_t frames_bad_crc = 0;
  uint32_t frames_sync_loss = 0;
};

typedef void (*SetpointHandler)(uint32_t timestamp_us, const Setpoint* sps, uint8_t count);
typedef void (*CommandHandler)(uint8_t cmd);

class SerialProtocol {
 public:
  void begin(unsigned long baud);
  void on_setpoints(SetpointHandler cb) { sp_cb_ = cb; }
  void on_command(CommandHandler cb) { cmd_cb_ = cb; }
  void poll();
  RxStats stats;
  uint32_t last_rx_us = 0;

 private:
  enum State { FIND_HEADER_1, FIND_HEADER_2, READ_FIXED, READ_ITEMS, READ_CRC1, READ_CRC2 };
  State state_ = FIND_HEADER_1;
  uint8_t version_ = 0;
  uint8_t type_ = 0;
  uint32_t seq_ = 0;
  uint32_t ts_us_ = 0;
  uint8_t count_ = 0;
  size_t item_bytes_expected_ = 0;
  uint8_t item_buf_[256];
  size_t item_buf_idx_ = 0;
  uint16_t crc_calc_ = 0xFFFF;
  uint16_t crc_rx_ = 0;
  Setpoint items_[MAX_MOTORS];
  uint8_t items_filled_ = 0;
  uint8_t fixed_idx_ = 0;
  uint8_t fixed_[11];
  SetpointHandler sp_cb_ = nullptr;
  CommandHandler cmd_cb_ = nullptr;

  void reset_state();
  void crc_update(uint8_t b);
};

uint16_t crc16_ccitt(const uint8_t* data, size_t len);

// TX helpers
void serial_send_telemetry(uint32_t rx_count, uint16_t can_rx_flags, uint32_t last_can_id, uint16_t status_flags);
void serial_send_error(uint16_t error_code);
