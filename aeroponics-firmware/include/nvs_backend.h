#pragma once

#include <cstdint>

/**
 * @brief Narrow boundary around ESP-IDF NVS calls used by NvsStorage.
 *
 * Keeping ESP-IDF result codes opaque here permits host tests to inject real
 * open/read fault outcomes without replacing the production repository.
 */
class INvsBackend {
public:
    using Result = int32_t;
    using Handle = uint32_t;

    virtual ~INvsBackend() = default;

    virtual Result flashInit() = 0;
    virtual Result flashErase() = 0;
    virtual bool isOk(Result result) const = 0;
    virtual bool isNotFound(Result result) const = 0;
    virtual bool requiresFlashErase(Result result) const = 0;
    virtual const char* errorName(Result result) const = 0;

    virtual Result open(const char* name_space, bool read_only, Handle& handle) = 0;
    virtual Result getU32(Handle handle, const char* key, uint32_t& value) = 0;
    virtual Result setU32(Handle handle, const char* key, uint32_t value) = 0;
    virtual Result commit(Handle handle) = 0;
    virtual Result eraseAll(Handle handle) = 0;
    virtual void close(Handle handle) = 0;
};
