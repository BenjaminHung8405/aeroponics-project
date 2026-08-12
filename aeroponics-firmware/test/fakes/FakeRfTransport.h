#pragma once

#include "core/IRfTransport.h"
#include <vector>
#include <cstring>
#include <algorithm>

/**
 * @brief Fake in-memory implementation of IRfTransport for unit testing RF framing and codecs.
 */
class FakeRfTransport : public IRfTransport {
public:
    FakeRfTransport() : _begin_called(false) {}
    ~FakeRfTransport() override = default;

    bool begin() override {
        _begin_called = true;
        return true;
    }

    size_t send(const uint8_t* data, size_t length) override {
        if (!_begin_called || !data || length == 0) return 0;
        _tx_buffer.insert(_tx_buffer.end(), data, data + length);
        return length;
    }

    size_t receive(uint8_t* buffer, size_t max_length) override {
        if (!_begin_called || !buffer || max_length == 0 || _rx_buffer.empty()) return 0;
        size_t count = std::min(max_length, _rx_buffer.size());
        std::copy(_rx_buffer.begin(), _rx_buffer.begin() + count, buffer);
        _rx_buffer.erase(_rx_buffer.begin(), _rx_buffer.begin() + count);
        return count;
    }

    size_t available() override {
        return _rx_buffer.size();
    }

    void flush() override {
        _tx_buffer.clear();
        _rx_buffer.clear();
    }

    // Helper methods for unit tests
    void injectRxData(const uint8_t* data, size_t length) {
        if (data && length > 0) {
            _rx_buffer.insert(_rx_buffer.end(), data, data + length);
        }
    }

    const std::vector<uint8_t>& getTxBuffer() const { return _tx_buffer; }
    bool isBeginCalled() const { return _begin_called; }

private:
    bool _begin_called;
    std::vector<uint8_t> _tx_buffer;
    std::vector<uint8_t> _rx_buffer;
};
