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
    explicit NvsStorage(INvsBackend* backend = nullptr);
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

    /**
     * @brief Get pointer to underlying NVS backend interface.
     */
    INvsBackend* backend() const { return backend_; }

private:
    INvsBackend* backend_;
    bool is_initialized_;
};
