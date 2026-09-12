#pragma once

#include <cstdint>
#include <cstddef>
#include "config.h"
#include "nvs_storage.h"
#include "rf_frame_codec.h"

enum class TreatmentStatus : uint8_t {
    DRAFT     = 0x00,
    PUBLISHED = 0x01,
    ARCHIVED  = 0x02
};

struct GroupProfile {
    uint32_t spray_day_s = 30;
    uint32_t cooldown_day_s = 300;
    uint32_t spray_night_s = 30;
    uint32_t cooldown_night_s = 600;

    constexpr GroupProfile() = default;
    constexpr GroupProfile(uint32_t sd, uint32_t cd, uint32_t sn, uint32_t cn)
        : spray_day_s(sd), cooldown_day_s(cd), spray_night_s(sn), cooldown_night_s(cn) {}

    bool isValid() const {
        return spray_day_s >= 5 && spray_day_s <= 300 &&
               cooldown_day_s >= 30 && cooldown_day_s <= 7200 &&
               spray_night_s >= 5 && spray_night_s <= 300 &&
               cooldown_night_s >= 30 && cooldown_night_s <= 7200;
    }
};

struct TreatmentSnapshot {
    uint32_t treatment_id = 0;
    uint32_t version_id = 0;
    uint32_t config_version = 0;
    TreatmentStatus status = TreatmentStatus::DRAFT;
    GroupProfile profile{};
    uint32_t published_at = 0;
    uint16_t checksum = 0;

    uint16_t computeChecksum() const {
        uint8_t buffer[32] = {};
        size_t offset = 0;
        auto writeU32 = [&buffer, &offset](uint32_t val) {
            buffer[offset++] = static_cast<uint8_t>(val & 0xFF);
            buffer[offset++] = static_cast<uint8_t>((val >> 8) & 0xFF);
            buffer[offset++] = static_cast<uint8_t>((val >> 16) & 0xFF);
            buffer[offset++] = static_cast<uint8_t>((val >> 24) & 0xFF);
        };
        writeU32(treatment_id);
        writeU32(version_id);
        writeU32(config_version);
        buffer[offset++] = static_cast<uint8_t>(status);
        writeU32(profile.spray_day_s);
        writeU32(profile.cooldown_day_s);
        writeU32(profile.spray_night_s);
        writeU32(profile.cooldown_night_s);
        writeU32(published_at);
        return RfFrameCodec::calculateCrc16(buffer, offset);
    }
};

/**
 * @brief Production Treatment Manager handling treatment snapshot validation,
 * monotonic versioning, atomic write-then-verify NVS persistence, and rollback.
 */
class TreatmentManager {
public:
    TreatmentManager();
    ~TreatmentManager() = default;

    /**
     * @brief Validate snapshot fields (PUBLISHED status, valid profile bounds, checksum).
     */
    bool validateSnapshot(const TreatmentSnapshot& snapshot) const;

    /**
     * @brief Apply and persist new published treatment version atomically with write-then-verify.
     * Rollbacks on any verification failure.
     */
    bool applyTreatmentVersion(const TreatmentSnapshot& snapshot, NvsStorage* nvs = nullptr);

    /**
     * @brief Load active treatment snapshot from NVS storage if available.
     */
    bool loadFromNvs(NvsStorage& nvs);

    /**
     * @brief Rollback to previous snapshot if currently available.
     */
    bool rollback();

    /**
     * @brief Get active treatment snapshot.
     */
    const TreatmentSnapshot& getActiveSnapshot() const { return active_snapshot_; }

    /**
     * @brief Check if a valid published treatment snapshot is active.
     */
    bool hasActiveSnapshot() const { return has_active_; }

    /**
     * @brief Get previous snapshot (if any).
     */
    const TreatmentSnapshot& getPreviousSnapshot() const { return previous_snapshot_; }
    bool hasPreviousSnapshot() const { return has_previous_; }

private:
    TreatmentSnapshot active_snapshot_{};
    TreatmentSnapshot previous_snapshot_{};
    bool has_active_ = false;
    bool has_previous_ = false;

    bool persistToNvs(NvsStorage& nvs, const TreatmentSnapshot& snapshot);
};
