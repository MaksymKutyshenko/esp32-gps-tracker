#pragma once

#include <stddef.h>

#include "esp_err.h"

// Where track records end up. The logger writes lines to named files and
// asks for recovery when writes keep failing; it never sees a card, a
// file system or a mount point. SdStorage implements this on the MicroSD
// card; the host tests use an in-memory fake.
class IStorage
{
public:
    virtual ~IStorage() = default;

    virtual bool isReady() const = 0;

    // Appends `length` bytes of `line` to `path`, creating the file with
    // `header` first if it did not exist.
    virtual esp_err_t append(const char *path, const char *header,
                             const char *line, size_t length) = 0;

    // Called after repeated append failures: remount, recreate directories,
    // whatever brings the medium back. Returns the new readiness.
    virtual esp_err_t recover() = 0;
};
