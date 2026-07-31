#pragma once

#include <cstdint>

/**
 * @brief Pure interface for Task Watchdog Timer operations.
 */
class IWatchdog {
public:
    virtual ~IWatchdog() = default;

    virtual bool registerWatchdog(uint8_t relay_id) = 0;
    virtual bool resetWatchdog(uint8_t relay_id) = 0;
    virtual bool deregisterWatchdog(uint8_t relay_id) = 0;
};
