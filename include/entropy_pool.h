#pragma once
#include <stddef.h>
#include <stdint.h>

// Single-threaded, one-use entropy queue. No generation, expansion or fallback.
class EntropyPool {
    uint8_t bytes_[64] = {};
    size_t head_ = 0, count_ = 0;
public:
    size_t available() const { return count_; }
    void clear() {
        for (volatile uint8_t *p = bytes_; p != bytes_ + sizeof(bytes_); ++p) *p = 0;
        head_ = count_ = 0;
    }
    bool append(const uint8_t *data, size_t length) {
        if ((!data && length) || length > sizeof(bytes_) - count_) return false;
        for (size_t i = 0; i < length; ++i) bytes_[(head_ + count_ + i) % sizeof(bytes_)] = data[i];
        count_ += length;
        return true;
    }
    int take(uint8_t *out, size_t length) {
        if ((!out && length) || length > count_) return -1;
        for (size_t i = 0; i < length; ++i) {
            out[i] = bytes_[head_];
            *static_cast<volatile uint8_t *>(&bytes_[head_]) = 0;
            head_ = (head_ + 1) % sizeof(bytes_);
        }
        count_ -= length;
        return 0;
    }
};
