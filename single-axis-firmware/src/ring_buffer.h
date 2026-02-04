#pragma once

#include <cstddef>
#include <cstdint>
#include <cstdlib>

struct Setpoint {
  uint32_t t_us;  // trajectory time in us
  float pos;
  float vel;
  float acc;
  float kp;
  float kd;
  float t_ff;
  uint16_t flags;
  uint8_t motor_id;
};

class RingBuffer {
 public:
  RingBuffer() : head_(0), tail_(0), size_(0) {}
  void init(size_t cap) {
    cap_ = cap;
    buf_ = (Setpoint*)malloc(sizeof(Setpoint) * cap_);
    head_ = tail_ = size_ = 0;
  }
  bool push(const Setpoint& sp) {
    if (size_ == cap_) return false;
    buf_[head_] = sp;
    head_ = (head_ + 1) % cap_;
    size_++;
    return true;
  }
  bool pop(Setpoint* out) {
    if (size_ == 0) return false;
    *out = buf_[tail_];
    tail_ = (tail_ + 1) % cap_;
    size_--;
    return true;
  }
  bool peek(size_t idx, Setpoint* out) const {
    if (idx >= size_) return false;
    size_t pos = (tail_ + idx) % cap_;
    *out = buf_[pos];
    return true;
  }
  size_t size() const { return size_;
  }
  void clear() { head_ = tail_ = size_ = 0; }
  bool empty() const { return size_ == 0; }

 private:
  Setpoint* buf_ = nullptr;
  size_t cap_ = 0;
  size_t head_ = 0;
  size_t tail_ = 0;
  size_t size_ = 0;
};
