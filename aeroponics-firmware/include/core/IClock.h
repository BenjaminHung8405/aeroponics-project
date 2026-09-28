#pragma once

#include <cstdint>

struct SystemTime {
    uint8_t hour;
    uint8_t minute;
    uint8_t second;
    bool is_valid;

    uint32_t toSecondsOfDay() const {
        return (static_cast<uint32_t>(hour) * 3600U) +
               (static_cast<uint32_t>(minute) * 60U) +
               static_cast<uint32_t>(second);
    }
};

/**
 * @brief Pure interface for time querying and night mode check.
 */
class IClock {
public:
    virtual ~IClock() = default;

    virtual SystemTime getTime() = 0;
    virtual bool isNightMode() = 0;
};
