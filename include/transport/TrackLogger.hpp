#pragma once

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

#include "esp_err.h"

#include "logic/TrackRecord.hpp"
#include "transport/IStorage.hpp"

// Writes track records as CSV, one file per UTC day. Retries a failed
// append a bounded number of times, then asks the storage to recover and
// tries once more; the record is reported lost only after all of that.
//
// Header-only and free of ESP-IDF calls (esp_err_t comes from a stub on
// the host) so the retry and recovery behaviour is unit-tested with a fake
// IStorage.
class TrackLogger
{
public:
    TrackLogger(IStorage &storage, const char *directory, uint8_t retries)
        : storage_(storage), directory_(directory), retries_(retries)
    {
    }

    // "<directory>/YYYYMMDD.CSV" — 8.3 names, FAT without long file names.
    void pathFor(const UtcTime &utc, char *out, size_t capacity) const
    {
        snprintf(out, capacity, "%s/%04u%02u%02u.CSV", directory_,
                 utc.year, utc.month, utc.day);
    }

    esp_err_t log(const TrackRecord &record)
    {
        char line[lineCapacity];
        const size_t length = TrackRecordFormat::csvLine(record, line, sizeof line);
        if (length == 0)
        {
            ++formatFailures_;
            return ESP_ERR_INVALID_SIZE;
        }

        char path[pathCapacity];
        pathFor(record.fix.utc, path, sizeof path);

        esp_err_t err = ESP_FAIL;
        for (uint8_t attempt = 0; attempt < retries_; ++attempt)
        {
            err = storage_.append(path, TrackRecordFormat::csvHeader, line, length);
            if (err == ESP_OK)
            {
                ++written_;
                return ESP_OK;
            }
            ++appendFailures_;
        }

        ++recoveries_;
        const esp_err_t recovered = storage_.recover();
        if (recovered != ESP_OK)
        {
            ++lost_;
            return recovered;
        }
        err = storage_.append(path, TrackRecordFormat::csvHeader, line, length);
        if (err == ESP_OK)
        {
            ++written_;
            return ESP_OK;
        }
        ++appendFailures_;
        ++lost_;
        return err;
    }

    uint32_t written() const { return written_; }
    uint32_t appendFailures() const { return appendFailures_; }
    uint32_t recoveries() const { return recoveries_; }
    uint32_t lost() const { return lost_; }
    uint32_t formatFailures() const { return formatFailures_; }

private:
    static constexpr size_t lineCapacity = 128;
    static constexpr size_t pathCapacity = 64;

    IStorage &storage_;
    const char *directory_;
    uint8_t retries_;
    uint32_t written_ = 0;
    uint32_t appendFailures_ = 0;
    uint32_t recoveries_ = 0;
    uint32_t lost_ = 0;
    uint32_t formatFailures_ = 0;
};
