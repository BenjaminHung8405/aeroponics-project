#pragma once

#include "core/IWatchdog.h"

/** ESP-IDF adapter. Its methods are called only by the owning relay task. */
class ESPTaskWatchdog : public IWatchdog {
public:
    bool registerWatchdog(uint8_t relay_id) override;
    bool resetWatchdog(uint8_t relay_id) override;
    bool deregisterWatchdog(uint8_t relay_id) override;
};
