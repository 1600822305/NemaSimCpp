#pragma once
// ================================================================
// RingBuffer<T> — fixed-capacity circular buffer for time-series data
// Used by DataBus to store historical values for plotting
// ================================================================

#include <vector>
#include <algorithm>
#include <cstddef>

namespace celegans {

template<typename T>
class RingBuffer {
public:
    explicit RingBuffer(size_t capacity = 60000)
        : capacity_(capacity) {
        data_.reserve(capacity);
    }

    void push(T value) {
        if (data_.size() < capacity_) {
            data_.push_back(value);
        } else {
            data_[write_pos_] = value;
        }
        write_pos_ = (write_pos_ + 1) % capacity_;
        count_ = std::min(count_ + 1, capacity_);
    }

    void clear() {
        data_.clear();
        write_pos_ = 0;
        count_ = 0;
    }

    size_t size() const { return count_; }
    size_t capacity() const { return capacity_; }
    bool empty() const { return count_ == 0; }

    // Access in chronological order: index 0 = oldest
    T operator[](size_t i) const {
        if (data_.size() < capacity_) {
            return data_[i];
        }
        return data_[(write_pos_ + i) % capacity_];
    }

    T back() const {
        if (data_.size() < capacity_) {
            return data_.back();
        }
        return data_[(write_pos_ + capacity_ - 1) % capacity_];
    }

    T front() const {
        if (data_.size() < capacity_) {
            return data_.front();
        }
        return data_[write_pos_ % capacity_];
    }

    // Get contiguous copy for plotting (ImPlot needs contiguous arrays)
    std::vector<T> to_vector() const {
        std::vector<T> result(count_);
        for (size_t i = 0; i < count_; ++i) {
            result[i] = (*this)[i];
        }
        return result;
    }

    // Get last N values as contiguous vector
    std::vector<T> last_n(size_t n) const {
        n = std::min(n, count_);
        std::vector<T> result(n);
        size_t start = count_ - n;
        for (size_t i = 0; i < n; ++i) {
            result[i] = (*this)[start + i];
        }
        return result;
    }

private:
    std::vector<T> data_;
    size_t capacity_;
    size_t write_pos_ = 0;
    size_t count_ = 0;
};

} // namespace celegans