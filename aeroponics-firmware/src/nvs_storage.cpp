#include "nvs_storage.h"

#include <cstdio>

#if defined(ESP_PLATFORM) || defined(ARDUINO)
#include "esp_log.h"
#include "nvs.h"
#include "nvs_flash.h"
#define NVS_LOGE(...) ESP_LOGE(TAG, __VA_ARGS__)
#define NVS_LOGI(...) ESP_LOGI(TAG, __VA_ARGS__)
#define NVS_LOGW(...) ESP_LOGW(TAG, __VA_ARGS__)
#else
#define NVS_LOGE(...) do {} while (false)
#define NVS_LOGI(...) do {} while (false)
#define NVS_LOGW(...) do {} while (false)
#endif

namespace {

constexpr char TAG[] = "NVS_STORAGE";

#if defined(ESP_PLATFORM) || defined(ARDUINO)
class EspIdfNvsBackend final : public INvsBackend {
public:
    Result flashInit() override { return nvs_flash_init(); }
    Result flashErase() override { return nvs_flash_erase(); }
    bool isOk(Result result) const override { return result == ESP_OK; }
    bool isNotFound(Result result) const override { return result == ESP_ERR_NVS_NOT_FOUND; }
    bool requiresFlashErase(Result result) const override {
        return result == ESP_ERR_NVS_NO_FREE_PAGES || result == ESP_ERR_NVS_NEW_VERSION_FOUND;
    }
    const char* errorName(Result result) const override { return esp_err_to_name(result); }

    Result open(const char* name_space, bool read_only, Handle& handle) override {
        nvs_handle_t native_handle = 0;
        const esp_err_t result = nvs_open(name_space, read_only ? NVS_READONLY : NVS_READWRITE, &native_handle);
        handle = static_cast<Handle>(native_handle);
        return result;
    }
    Result getU32(Handle handle, const char* key, uint32_t& value) override {
        return nvs_get_u32(static_cast<nvs_handle_t>(handle), key, &value);
    }
    Result setU32(Handle handle, const char* key, uint32_t value) override {
        return nvs_set_u32(static_cast<nvs_handle_t>(handle), key, value);
    }
    Result getBlob(Handle handle, const char* key, void* out_data, size_t* inout_len) override {
        return nvs_get_blob(static_cast<nvs_handle_t>(handle), key, out_data, inout_len);
    }
    Result setBlob(Handle handle, const char* key, const void* data, size_t len) override {
        return nvs_set_blob(static_cast<nvs_handle_t>(handle), key, data, len);
    }
    Result commit(Handle handle) override { return nvs_commit(static_cast<nvs_handle_t>(handle)); }
    Result eraseAll(Handle handle) override { return nvs_erase_all(static_cast<nvs_handle_t>(handle)); }
    void close(Handle handle) override { nvs_close(static_cast<nvs_handle_t>(handle)); }
};

EspIdfNvsBackend default_backend;
#endif

} // namespace

NvsStorage::NvsStorage(INvsBackend* backend, const char* name_space)
    : backend_(backend), name_space_(name_space), is_initialized_(false) {
#if defined(ESP_PLATFORM) || defined(ARDUINO)
    if (backend_ == nullptr) backend_ = &default_backend;
#endif
}

NvsStorage::~NvsStorage() = default;

bool NvsStorage::begin() {
    if (backend_ == nullptr || name_space_ == nullptr || name_space_[0] == '\0') return false;

    INvsBackend::Result result = backend_->flashInit();
    if (backend_->requiresFlashErase(result)) {
        NVS_LOGW("NVS partition requires erase before initialization");
        if (!backend_->isOk(backend_->flashErase())) {
            is_initialized_ = false;
            return false;
        }
        result = backend_->flashInit();
    }

    is_initialized_ = backend_->isOk(result);
    if (is_initialized_) NVS_LOGI("NVS storage initialized successfully");
    return is_initialized_;
}

bool NvsStorage::factoryReset() {
    if (!is_initialized_ || backend_ == nullptr) {
        NVS_LOGE("Factory reset failed: NVS storage not initialized");
        return false;
    }

    INvsBackend::Handle handle = 0;
    INvsBackend::Result result = backend_->open(name_space_, false, handle);
    if (!backend_->isOk(result)) {
        NVS_LOGE("Factory reset failed: Unable to open namespace '%s': %s (%ld)",
                 name_space_, backend_->errorName(result), static_cast<long>(result));
        return false;
    }

    result = backend_->eraseAll(handle);
    if (!backend_->isOk(result)) {
        NVS_LOGE("Factory reset failed: Erase all returned %s (%ld)",
                 backend_->errorName(result), static_cast<long>(result));
        backend_->close(handle);
        return false;
    }

    result = backend_->commit(handle);
    backend_->close(handle);
    if (!backend_->isOk(result)) {
        NVS_LOGE("Factory reset failed: Commit returned %s (%ld)",
                 backend_->errorName(result), static_cast<long>(result));
        return false;
    }

    NVS_LOGI("Factory reset successfully erased namespace '%s'", name_space_);
    return true;
}

bool NvsStorage::getU32(const char* key, uint32_t& value) const {
    if (!is_initialized_ || backend_ == nullptr || key == nullptr) return false;
    INvsBackend::Handle handle = 0;
    if (!backend_->isOk(backend_->open(name_space_, true, handle))) return false;
    const INvsBackend::Result result = backend_->getU32(handle, key, value);
    backend_->close(handle);
    return backend_->isOk(result);
}

bool NvsStorage::setU32(const char* key, uint32_t value) {
    if (!is_initialized_ || backend_ == nullptr || key == nullptr) return false;
    INvsBackend::Handle handle = 0;
    if (!backend_->isOk(backend_->open(name_space_, false, handle))) return false;
    const bool ok = backend_->isOk(backend_->setU32(handle, key, value)) &&
                    backend_->isOk(backend_->commit(handle));
    backend_->close(handle);
    return ok;
}

bool NvsStorage::getBlob(const char* key, void* out_data, size_t* inout_len) const {
    if (!is_initialized_ || backend_ == nullptr || key == nullptr || out_data == nullptr || inout_len == nullptr) {
        return false;
    }
    INvsBackend::Handle handle = 0;
    if (!backend_->isOk(backend_->open(name_space_, true, handle))) return false;
    const INvsBackend::Result result = backend_->getBlob(handle, key, out_data, inout_len);
    backend_->close(handle);
    return backend_->isOk(result);
}

bool NvsStorage::setBlob(const char* key, const void* data, size_t len) {
    if (!is_initialized_ || backend_ == nullptr || key == nullptr || data == nullptr || len == 0) {
        return false;
    }
    INvsBackend::Handle handle = 0;
    if (!backend_->isOk(backend_->open(name_space_, false, handle))) return false;
    const bool ok = backend_->isOk(backend_->setBlob(handle, key, data, len)) &&
                    backend_->isOk(backend_->commit(handle));
    backend_->close(handle);
    return ok;
}

