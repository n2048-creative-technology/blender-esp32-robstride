#include "protocol_serial.h"

#include <cstring>

#include "serial_compat.h"

static const uint8_t HDR1 = 0xA5;
static const uint8_t HDR2 = 0x5A;

uint16_t crc16_ccitt(const uint8_t* data, size_t len) {
  uint16_t crc = 0xFFFF;
  for (size_t i = 0; i < len; ++i) {
    crc ^= (uint16_t)data[i] << 8;
    for (int j = 0; j < 8; ++j) {
      if (crc & 0x8000) {
        crc = (crc << 1) ^ 0x1021;
      } else {
        crc <<= 1;
      }
    }
  }
  return crc;
}

void SerialProtocol::begin(unsigned long baud) {
  Serial.begin(static_cast<uint32_t>(baud));
}

void SerialProtocol::reset_state() {
  state_ = FIND_HEADER_1;
  items_filled_ = 0;
  item_buf_idx_ = 0;
  crc_calc_ = 0xFFFF;
  fixed_idx_ = 0;
}

void SerialProtocol::crc_update(uint8_t b) {
  crc_calc_ ^= (uint16_t)b << 8;
  for (int i = 0; i < 8; ++i) {
    if (crc_calc_ & 0x8000) {
      crc_calc_ = (crc_calc_ << 1) ^ 0x1021;
    } else {
      crc_calc_ <<= 1;
    }
  }
}

void SerialProtocol::poll() {
  while (Serial.available() > 0) {
    int r = Serial.read();
    if (r < 0) break;
    uint8_t b = static_cast<uint8_t>(r);
    switch (state_) {
      case FIND_HEADER_1:
        if (b == HDR1) state_ = FIND_HEADER_2;
        break;
      case FIND_HEADER_2:
        if (b == HDR2) {
          state_ = READ_FIXED;
          crc_calc_ = 0xFFFF;
          fixed_idx_ = 0;
        } else {
          state_ = FIND_HEADER_1;
          stats.frames_sync_loss++;
        }
        break;
      case READ_FIXED: {
        fixed_[fixed_idx_++] = b;
        if (fixed_idx_ == sizeof(fixed_)) {
          for (uint8_t i = 0; i < sizeof(fixed_); ++i) crc_update(fixed_[i]);
          version_ = fixed_[0];
          type_ = fixed_[1];
          std::memcpy(&seq_, &fixed_[2], 4);
          std::memcpy(&ts_us_, &fixed_[6], 4);
          count_ = fixed_[10];
          fixed_idx_ = 0;
          if (type_ == MSG_SETPOINTS) {
            items_filled_ = 0;
            item_buf_idx_ = 0;
            item_bytes_expected_ = static_cast<size_t>(count_) * (1 + 6 * 4 + 2);
            if (item_bytes_expected_ > sizeof(item_buf_)) {
              item_bytes_expected_ = sizeof(item_buf_);
            }
            state_ = READ_ITEMS;
          } else if (type_ == MSG_COMMAND) {
            item_bytes_expected_ = 2;
            item_buf_idx_ = 0;
            state_ = READ_ITEMS;
          } else {
            reset_state();
          }
        }
        break; }
      case READ_ITEMS: {
        item_buf_[item_buf_idx_++] = b;
        if (item_buf_idx_ == item_bytes_expected_) {
          if (type_ == MSG_SETPOINTS) {
            uint8_t offset = 0;
            const uint8_t stride = (1 + 6 * 4 + 2);
            for (uint8_t i = 0; i < count_ && i < MAX_MOTORS; ++i) {
              const uint8_t* p = item_buf_ + offset;
              Setpoint sp{};
              sp.motor_id = p[0];
              std::memcpy(&sp.pos, p + 1, 4);
              std::memcpy(&sp.vel, p + 5, 4);
              std::memcpy(&sp.acc, p + 9, 4);
              std::memcpy(&sp.kp, p + 13, 4);
              std::memcpy(&sp.kd, p + 17, 4);
              std::memcpy(&sp.t_ff, p + 21, 4);
              std::memcpy(&sp.flags, p + 25, 2);
              sp.t_us = ts_us_;
              items_[items_filled_++] = sp;
              offset += stride;
            }
            for (size_t i = 0; i < item_buf_idx_; ++i) crc_update(item_buf_[i]);
          } else if (type_ == MSG_COMMAND) {
            for (size_t i = 0; i < item_buf_idx_; ++i) crc_update(item_buf_[i]);
          }
          state_ = READ_CRC1;
        }
        break; }
      case READ_CRC1:
        crc_rx_ = b;
        state_ = READ_CRC2;
        break;
      case READ_CRC2: {
        crc_rx_ |= (uint16_t)b << 8;
        if (crc_rx_ == crc_calc_) {
          stats.frames_ok++;
          if (type_ == MSG_SETPOINTS && sp_cb_) {
            sp_cb_(ts_us_, items_, items_filled_);
          } else if (type_ == MSG_COMMAND && cmd_cb_) {
            uint8_t cmd = item_buf_[0];
            uint8_t motor_id = item_buf_[1];
            cmd_cb_(cmd, motor_id);
          }
        } else {
          stats.frames_bad_crc++;
        }
        reset_state();
        break; }
    }
  }
}

void serial_send_telemetry(uint8_t motor_id, uint32_t rx_count, uint16_t can_rx_flags, uint32_t last_can_id, uint16_t status_flags) {
  uint8_t payload[1 + 1 + 4 + 4 + 1 + 1 + 4 + 2 + 4 + 2];
  uint8_t* p = payload;
  *p++ = 1;
  *p++ = MSG_TELEMETRY;
  uint32_t seq = 0;
  std::memcpy(p, &seq, 4);
  p += 4;
  uint32_t ts = 0;
  std::memcpy(p, &ts, 4);
  p += 4;
  *p++ = 1;
  *p++ = motor_id;
  std::memcpy(p, &rx_count, 4);
  p += 4;
  std::memcpy(p, &can_rx_flags, 2);
  p += 2;
  std::memcpy(p, &last_can_id, 4);
  p += 4;
  std::memcpy(p, &status_flags, 2);
  p += 2;
  uint16_t crc = crc16_ccitt(payload, sizeof(payload));
  uint8_t hdr[2] = {0xA5, 0x5A};
  Serial.write(hdr, 2);
  Serial.write(payload, sizeof(payload));
  Serial.write(reinterpret_cast<uint8_t*>(&crc), 2);
}

void serial_send_error(uint8_t motor_id, uint16_t error_code) {
  uint8_t payload[1 + 1 + 4 + 4 + 1 + 1 + 2];
  uint8_t* p = payload;
  *p++ = 1;
  *p++ = MSG_ERROR;
  uint32_t seq = 0;
  std::memcpy(p, &seq, 4);
  p += 4;
  uint32_t ts = 0;
  std::memcpy(p, &ts, 4);
  p += 4;
  *p++ = 1;
  *p++ = motor_id;
  std::memcpy(p, &error_code, 2);
  p += 2;
  uint16_t crc = crc16_ccitt(payload, sizeof(payload));
  uint8_t hdr[2] = {0xA5, 0x5A};
  Serial.write(hdr, 2);
  Serial.write(payload, sizeof(payload));
  Serial.write(reinterpret_cast<uint8_t*>(&crc), 2);
}
