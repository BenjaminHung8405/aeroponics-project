#pragma once

#include <cstring>
#include "nvs_backend.h"

#if defined(LEGACY_RELAY_SUPPORT)
#include "prototype/legacy_relay/legacy_relay_config.h"
#endif

class FakeNvsBackend final : public INvsBackend {
public:
    static constexpr Result OK = 0;
    static constexpr Result NOT_FOUND = 1;
    static constexpr Result IO_ERROR = -1;

    Result flashInit() override { return flash_init_result_; }
    Result flashErase() override { return OK; }
    bool isOk(Result result) const override { return result == OK; }
    bool isNotFound(Result result) const override { return result == NOT_FOUND; }
    bool requiresFlashErase(Result result) const override { return false; }
    const char* errorName(Result result) const override {
        return result == IO_ERROR ? "IO_ERROR" : "UNKNOWN";
    }

    Result open(const char*, bool, Handle& handle) override {
        ++open_calls_;
        handle = 1;
        return open_result_;
    }

    Result getU32(Handle, const char* key, uint32_t& value) override {
        ++get_calls_;
        const uint8_t field = fieldForKey(key);
        value = values_[field];
        return get_results_[field];
    }

    Result setU32(Handle, const char* key, uint32_t value) override {
        const uint8_t field = fieldForKey(key);
        pending_values_[field] = value;
        pending_write_[field] = true;
        return set_results_[field];
    }
    Result commit(Handle) override {
        if (commit_result_ != OK) return commit_result_;
        for (uint8_t field = 0; field < FIELD_COUNT; ++field) {
            if (pending_write_[field]) values_[field] = pending_values_[field];
        }
        std::memset(pending_write_, 0, sizeof(pending_write_));
        return OK;
    }
    Result eraseAll(Handle) override { return OK; }
    void close(Handle) override { ++close_calls_; }

    void setOpenResult(Result result) { open_result_ = result; }
    void setGetResult(uint8_t field, Result result) { if (field < FIELD_COUNT) get_results_[field] = result; }
    void setSetResult(uint8_t field, Result result) { if (field < FIELD_COUNT) set_results_[field] = result; }
    void setCommitResult(Result result) { commit_result_ = result; }
    void setValue(uint8_t field, uint32_t value) { if (field < FIELD_COUNT) values_[field] = value; }
    uint32_t openCalls() const { return open_calls_; }
    uint32_t getCalls() const { return get_calls_; }

    enum Field : uint8_t { SPRAY_DAY, COOLDOWN_DAY, SPRAY_NIGHT, COOLDOWN_NIGHT, FIELD_COUNT };

private:
    static uint8_t fieldForKey(const char* key) {
        if (std::strncmp(key, "sd_", 3) == 0) return SPRAY_DAY;
        if (std::strncmp(key, "cd_", 3) == 0) return COOLDOWN_DAY;
        if (std::strncmp(key, "sn_", 3) == 0) return SPRAY_NIGHT;
        if (std::strncmp(key, "rf_boot", 7) == 0) return SPRAY_DAY;
        if (std::strncmp(key, "rf_psk_0", 8) == 0) return COOLDOWN_DAY;
        if (std::strncmp(key, "rf_psk_1", 8) == 0) return SPRAY_NIGHT;
        if (std::strncmp(key, "rf_psk_2", 8) == 0) return COOLDOWN_NIGHT;
        return COOLDOWN_NIGHT;
    }

    Result flash_init_result_ = OK;
    Result open_result_ = OK;
    Result get_results_[FIELD_COUNT] = {OK, OK, OK, OK};
    Result set_results_[FIELD_COUNT] = {OK, OK, OK, OK};
    Result commit_result_ = OK;
    uint32_t values_[FIELD_COUNT] = {30, 300, 30, 600};
    uint32_t pending_values_[FIELD_COUNT] = {};
    bool pending_write_[FIELD_COUNT] = {};
    uint32_t open_calls_ = 0;
    uint32_t get_calls_ = 0;
    uint32_t close_calls_ = 0;
};
