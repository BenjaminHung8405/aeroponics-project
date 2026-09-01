#pragma once

#include "node_command_processor.h"

/**
 * @brief Fixed-record EEPROM adapter for an ATmega8 node-local schedule.
 *
 * The node owns this record; gateway temporary overrides never call save().
 * Invalid, missing, or torn records are rejected and therefore leave the
 * schedule disabled (fail-closed).
 */
class Atmega8EepromScheduleStorage final : public INodeScheduleStorage {
public:
    bool load(uint8_t node_id, NodeScheduleProfile& profile) override;
    bool save(uint8_t node_id, const NodeScheduleProfile& profile) override;
};
