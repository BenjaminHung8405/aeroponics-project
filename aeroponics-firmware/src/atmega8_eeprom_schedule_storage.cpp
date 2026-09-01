#include "atmega8_eeprom_schedule_storage.h"

#include <cstddef>
#include <cstdint>
#include <cstring>

#if defined(__AVR__)
#include <avr/eeprom.h>
#endif

namespace {
constexpr uint16_t MAGIC = 0xA85A;
constexpr uint8_t VERSION = 1;
constexpr uint16_t EEPROM_BASE = 0;

struct EepromScheduleRecord {
    uint16_t magic;
    uint8_t version;
    uint8_t node_id;
    uint32_t spray_duration_ms;
    uint32_t cooldown_duration_ms;
    uint8_t schedule_enabled;
    uint8_t checksum;
};

uint8_t checksum(const EepromScheduleRecord& record) {
    const auto* bytes = reinterpret_cast<const uint8_t*>(&record);
    uint8_t value = 0;
    for (size_t i = 0; i < offsetof(EepromScheduleRecord, checksum); ++i) value ^= bytes[i];
    return value;
}

uint16_t addressFor(uint8_t node_id) {
    return static_cast<uint16_t>(EEPROM_BASE + (node_id - 1U) * sizeof(EepromScheduleRecord));
}
} // namespace

bool Atmega8EepromScheduleStorage::load(uint8_t node_id, NodeScheduleProfile& profile) {
    if (node_id < 1 || node_id > 4) return false;
#if defined(__AVR__)
    EepromScheduleRecord record{};
    eeprom_read_block(&record, reinterpret_cast<const void*>(addressFor(node_id)), sizeof(record));
    if (record.magic != MAGIC || record.version != VERSION || record.node_id != node_id ||
        record.checksum != checksum(record) || record.spray_duration_ms == 0 ||
        record.cooldown_duration_ms == 0 || record.schedule_enabled > 1) return false;
    profile.spray_duration_ms = record.spray_duration_ms;
    profile.cooldown_duration_ms = record.cooldown_duration_ms;
    profile.schedule_enabled = record.schedule_enabled != 0;
    return true;
#else
    (void)profile;
    return false;
#endif
}

bool Atmega8EepromScheduleStorage::save(uint8_t node_id, const NodeScheduleProfile& profile) {
    if (node_id < 1 || node_id > 4 || profile.spray_duration_ms == 0 ||
        profile.cooldown_duration_ms == 0) return false;
#if defined(__AVR__)
    EepromScheduleRecord record{};
    record.magic = MAGIC;
    record.version = VERSION;
    record.node_id = node_id;
    record.spray_duration_ms = profile.spray_duration_ms;
    record.cooldown_duration_ms = profile.cooldown_duration_ms;
    record.schedule_enabled = profile.schedule_enabled ? 1 : 0;
    record.checksum = checksum(record);
    eeprom_update_block(&record, reinterpret_cast<void*>(addressFor(node_id)), sizeof(record));
    return true;
#else
    (void)profile;
    return false;
#endif
}
