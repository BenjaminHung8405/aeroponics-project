#pragma once

#include <cstddef>
#include <cstdint>

/**
 * @brief Abstract interface for RF transport layer (e.g. UART adapter, LoRa/HC-12 433 MHz module).
 *
 * Decouples physical RF hardware/UART transport from application framing, codecs, and business logic.
 */
class IRfTransport {
public:
    virtual ~IRfTransport() = default;

    /**
     * @brief Initialize hardware transport (e.g. UART pins, baud rate).
     * @return true if initialized successfully, false otherwise.
     */
    virtual bool begin() = 0;

    /**
     * @brief Transmit data bytes over the RF interface.
     * @param data Pointer to payload buffer.
     * @param length Number of bytes to send.
     * @return Number of bytes transmitted successfully.
     */
    virtual size_t send(const uint8_t* data, size_t length) = 0;

    /**
     * @brief Receive incoming bytes from the RF interface into a buffer (non-blocking).
     * @param buffer Output buffer to receive bytes.
     * @param max_length Maximum bytes to read.
     * @return Number of bytes read.
     */
    virtual size_t receive(uint8_t* buffer, size_t max_length) = 0;

    /**
     * @brief Check number of bytes available to read from RX interface.
     * @return Available byte count.
     */
    virtual size_t available() = 0;

    /**
     * @brief Flush transport buffers.
     */
    virtual void flush() = 0;
};
