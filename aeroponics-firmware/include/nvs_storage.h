#pragma once

#include <cstdint>
#include "config.h"
#include "nvs_backend.h"

/**
 * @brief ESP-IDF implementation of NVS storage backend.
 * Manages persistent storage in NVS flash namespace 'aeroponics'.
 */
class NvsStorage {
public:
    explicit NvsStorage(INvsBackend* backend = nullptr, const char* name_space = "aeroponics");
    virtual ~NvsStorage();

    /**
     * @brief Initialize NVS flash and open the storage namespace.
     * @return true if initialized successfully, false otherwise.
     */
    bool begin();

    /**
     * @brief Perform factory reset by erasing the 'aeroponics' NVS storage namespace.
     * @return true on successful erasure, false otherwise.
     */
    bool factoryReset();
    bool isInitialized() const { return is_initialized_; }
    bool getU32(const char* key, uint32_t& value) const;
    bool setU32(const char* key, uint32_t value);
    bool getBlob(const char* key, void* out_data, size_t* inout_len) const;
    bool setBlob(const char* key, const void* data, size_t len);

    /**
     * @brief Get pointer to underlying NVS backend interface.
     */
    INvsBackend* backend() const { return backend_; }

private:
    INvsBackend* backend_;
    const char* name_space_;
    bool is_initialized_;
};
