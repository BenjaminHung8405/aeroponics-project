#pragma once

#include <chrono>
#include <condition_variable>
#include <mutex>

#include "core/IProfileRepository.h"

class FakeProfileRepository : public IProfileRepository {
public:
    FakeProfileRepository() {
        for (uint8_t i = 0; i < TOTAL_RELAYS; ++i) {
            profiles_[i] = RelayProfile{ DEFAULT_SPRAY_DAY_S, DEFAULT_COOLDOWN_DAY_S, DEFAULT_SPRAY_NIGHT_S, DEFAULT_COOLDOWN_NIGHT_S };
        }
    }

    bool loadProfile(uint8_t relay_id, RelayProfile &profile) override {
        if (relay_id >= TOTAL_RELAYS) return false;
        if (load_failure_[relay_id]) {
            profile = defaultProfile();
            return false;
        }
        profile = profiles_[relay_id];
        return true;
    }

    bool saveProfile(uint8_t relay_id, const RelayProfile &profile) override {
        if (relay_id >= TOTAL_RELAYS) return false;
        waitUntilSaveReleased();
        if (save_failure_[relay_id]) return false;
        if (profile.spray_day_s < MIN_SPRAY_DURATION_S || profile.spray_day_s > MAX_SPRAY_DURATION_S) return false;
        if (profile.cooldown_day_s < MIN_COOLDOWN_DURATION_S || profile.cooldown_day_s > MAX_COOLDOWN_DURATION_S) return false;
        if (profile.spray_night_s < MIN_SPRAY_DURATION_S || profile.spray_night_s > MAX_SPRAY_DURATION_S) return false;
        if (profile.cooldown_night_s < MIN_COOLDOWN_DURATION_S || profile.cooldown_night_s > MAX_COOLDOWN_DURATION_S) return false;

        profiles_[relay_id] = profile;
        return true;
    }

    bool loadAllProfiles(RelayProfile profiles[TOTAL_RELAYS]) override {
        if (profiles == nullptr) return false;
        bool all_loaded = true;
        for (uint8_t i = 0; i < TOTAL_RELAYS; ++i) {
            if (!loadProfile(i, profiles[i])) {
                all_loaded = false;
            }
        }
        return all_loaded;
    }

    bool factoryReset() override {
        for (uint8_t i = 0; i < TOTAL_RELAYS; ++i) {
            profiles_[i] = RelayProfile{ DEFAULT_SPRAY_DAY_S, DEFAULT_COOLDOWN_DAY_S, DEFAULT_SPRAY_NIGHT_S, DEFAULT_COOLDOWN_NIGHT_S };
        }
        return true;
    }

    void setSaveFailure(uint8_t relay_id, bool should_fail) {
        if (relay_id < TOTAL_RELAYS) save_failure_[relay_id] = should_fail;
    }

    void setLoadFailure(uint8_t relay_id, bool should_fail) {
        if (relay_id < TOTAL_RELAYS) load_failure_[relay_id] = should_fail;
    }

    void blockSaves() {
        std::lock_guard<std::mutex> lock(save_mutex_);
        block_saves_ = true;
        save_entered_ = false;
    }

    bool waitForSaveToStart(uint32_t timeout_ms) {
        std::unique_lock<std::mutex> lock(save_mutex_);
        return save_condition_.wait_for(
            lock, std::chrono::milliseconds(timeout_ms), [this] { return save_entered_; });
    }

    void releaseSaves() {
        {
            std::lock_guard<std::mutex> lock(save_mutex_);
            block_saves_ = false;
        }
        save_condition_.notify_all();
    }

private:
    static RelayProfile defaultProfile() {
        return RelayProfile{ DEFAULT_SPRAY_DAY_S, DEFAULT_COOLDOWN_DAY_S,
                             DEFAULT_SPRAY_NIGHT_S, DEFAULT_COOLDOWN_NIGHT_S };
    }

    void waitUntilSaveReleased() {
        std::unique_lock<std::mutex> lock(save_mutex_);
        if (!block_saves_) return;
        save_entered_ = true;
        save_condition_.notify_all();
        save_condition_.wait(lock, [this] { return !block_saves_; });
    }

    RelayProfile profiles_[TOTAL_RELAYS];
    bool save_failure_[TOTAL_RELAYS] = {};
    bool load_failure_[TOTAL_RELAYS] = {};
    std::mutex save_mutex_;
    std::condition_variable save_condition_;
    bool block_saves_ = false;
    bool save_entered_ = false;
};
