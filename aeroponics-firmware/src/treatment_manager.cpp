#include "treatment_manager.h"

namespace {
constexpr const char* NVS_KEY_TR_ID     = "tr_id";
constexpr const char* NVS_KEY_TR_VID    = "tr_vid";
constexpr const char* NVS_KEY_TR_CVER   = "tr_cver";
constexpr const char* NVS_KEY_TR_SP_D   = "tr_sp_d";
constexpr const char* NVS_KEY_TR_CD_D   = "tr_cd_d";
constexpr const char* NVS_KEY_TR_SP_N   = "tr_sp_n";
constexpr const char* NVS_KEY_TR_CD_N   = "tr_cd_n";
constexpr const char* NVS_KEY_TR_PUB    = "tr_pub";
constexpr const char* NVS_KEY_TR_CRC    = "tr_crc";
}

TreatmentManager::TreatmentManager()
    : has_active_(false), has_previous_(false) {}

bool TreatmentManager::validateSnapshot(const TreatmentSnapshot& snapshot) const {
    // 1. Status must be PUBLISHED
    if (snapshot.status != TreatmentStatus::PUBLISHED) {
        return false;
    }

    // 2. IDs and config version must be non-zero
    if (snapshot.treatment_id == 0 || snapshot.version_id == 0 || snapshot.config_version == 0) {
        return false;
    }

    // 3. Profile durations must be within bounded limits
    if (!snapshot.profile.isValid()) {
        return false;
    }

    // 4. Checksum verification
    if (snapshot.checksum == 0 || snapshot.checksum != snapshot.computeChecksum()) {
        return false;
    }

    // 5. Monotonic versioning check if active snapshot exists
    if (has_active_ && snapshot.config_version <= active_snapshot_.config_version) {
        return false;
    }

    return true;
}

bool TreatmentManager::persistToNvs(NvsStorage& nvs, const TreatmentSnapshot& snapshot) {
    // 1. Write fields
    if (!nvs.setU32(NVS_KEY_TR_ID, snapshot.treatment_id) ||
        !nvs.setU32(NVS_KEY_TR_VID, snapshot.version_id) ||
        !nvs.setU32(NVS_KEY_TR_CVER, snapshot.config_version) ||
        !nvs.setU32(NVS_KEY_TR_SP_D, snapshot.profile.spray_day_s) ||
        !nvs.setU32(NVS_KEY_TR_CD_D, snapshot.profile.cooldown_day_s) ||
        !nvs.setU32(NVS_KEY_TR_SP_N, snapshot.profile.spray_night_s) ||
        !nvs.setU32(NVS_KEY_TR_CD_N, snapshot.profile.cooldown_night_s) ||
        !nvs.setU32(NVS_KEY_TR_PUB, snapshot.published_at) ||
        !nvs.setU32(NVS_KEY_TR_CRC, snapshot.checksum)) {
        return false;
    }

    // 2. Read back and verify (write-then-verify)
    uint32_t val = 0;
    if (!nvs.getU32(NVS_KEY_TR_ID, val) || val != snapshot.treatment_id) return false;
    if (!nvs.getU32(NVS_KEY_TR_VID, val) || val != snapshot.version_id) return false;
    if (!nvs.getU32(NVS_KEY_TR_CVER, val) || val != snapshot.config_version) return false;
    if (!nvs.getU32(NVS_KEY_TR_SP_D, val) || val != snapshot.profile.spray_day_s) return false;
    if (!nvs.getU32(NVS_KEY_TR_CD_D, val) || val != snapshot.profile.cooldown_day_s) return false;
    if (!nvs.getU32(NVS_KEY_TR_SP_N, val) || val != snapshot.profile.spray_night_s) return false;
    if (!nvs.getU32(NVS_KEY_TR_CD_N, val) || val != snapshot.profile.cooldown_night_s) return false;
    if (!nvs.getU32(NVS_KEY_TR_PUB, val) || val != snapshot.published_at) return false;
    if (!nvs.getU32(NVS_KEY_TR_CRC, val) || val != snapshot.checksum) return false;

    return true;
}

bool TreatmentManager::applyTreatmentVersion(const TreatmentSnapshot& snapshot, NvsStorage* nvs) {
    if (!validateSnapshot(snapshot)) {
        return false;
    }

    // If NVS is provided, perform atomic write-then-verify
    if (nvs != nullptr) {
        if (!persistToNvs(*nvs, snapshot)) {
            // Verification failed: rollback NVS to previous active snapshot if available
            if (has_active_) {
                persistToNvs(*nvs, active_snapshot_);
            }
            return false;
        }
    }

    // Update in-memory state
    if (has_active_) {
        previous_snapshot_ = active_snapshot_;
        has_previous_ = true;
    }
    active_snapshot_ = snapshot;
    has_active_ = true;
    return true;
}

bool TreatmentManager::loadFromNvs(NvsStorage& nvs) {
    TreatmentSnapshot snap{};
    uint32_t val = 0;

    if (!nvs.getU32(NVS_KEY_TR_ID, snap.treatment_id) ||
        !nvs.getU32(NVS_KEY_TR_VID, snap.version_id) ||
        !nvs.getU32(NVS_KEY_TR_CVER, snap.config_version) ||
        !nvs.getU32(NVS_KEY_TR_SP_D, snap.profile.spray_day_s) ||
        !nvs.getU32(NVS_KEY_TR_CD_D, snap.profile.cooldown_day_s) ||
        !nvs.getU32(NVS_KEY_TR_SP_N, snap.profile.spray_night_s) ||
        !nvs.getU32(NVS_KEY_TR_CD_N, snap.profile.cooldown_night_s) ||
        !nvs.getU32(NVS_KEY_TR_PUB, snap.published_at) ||
        !nvs.getU32(NVS_KEY_TR_CRC, val)) {
        return false;
    }
    snap.checksum = static_cast<uint16_t>(val);
    snap.status = TreatmentStatus::PUBLISHED;

    if (!snap.profile.isValid() || snap.checksum != snap.computeChecksum()) {
        return false;
    }

    active_snapshot_ = snap;
    has_active_ = true;
    return true;
}

bool TreatmentManager::rollback() {
    if (!has_previous_) {
        return false;
    }
    active_snapshot_ = previous_snapshot_;
    has_previous_ = false;
    return true;
}
