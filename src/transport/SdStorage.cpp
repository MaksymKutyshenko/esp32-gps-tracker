#include "transport/SdStorage.hpp"

#include <errno.h>
#include <stdio.h>
#include <sys/stat.h>
#include <unistd.h>

#include "esp_log.h"

namespace
{
    constexpr const char *logTag = "storage";
}

SdStorage::SdStorage(SdCard &card, const char *directory)
    : card_(card), directory_(directory)
{
}

esp_err_t SdStorage::begin()
{
    esp_err_t err = card_.mount();
    if (err != ESP_OK)
    {
        ESP_LOGW(logTag, "No card at boot (%s); logging waits for recovery", esp_err_to_name(err));
        return err;
    }
    return ensureDirectory();
}

esp_err_t SdStorage::ensureDirectory()
{
    struct stat info = {};
    if (stat(directory_, &info) == 0)
    {
        return ESP_OK;
    }
    if (mkdir(directory_, 0775) != 0 && errno != EEXIST)
    {
        ESP_LOGE(logTag, "mkdir %s failed: errno %d", directory_, errno);
        return ESP_FAIL;
    }
    return ESP_OK;
}

esp_err_t SdStorage::append(const char *path, const char *header,
                            const char *line, size_t length)
{
    if (!card_.isMounted())
    {
        return ESP_ERR_INVALID_STATE;
    }

    struct stat info = {};
    const bool isNew = stat(path, &info) != 0;

    FILE *file = fopen(path, "a");
    if (file == nullptr)
    {
        return ESP_FAIL;
    }

    esp_err_t err = ESP_OK;
    if (isNew && fputs(header, file) < 0)
    {
        err = ESP_FAIL;
    }
    if (err == ESP_OK && fwrite(line, 1, length, file) != length)
    {
        err = ESP_FAIL;
    }
    // fflush pushes the C buffer to FATFS, fsync pushes FATFS to the card;
    // without the second a pulled card still loses the line.
    if (err == ESP_OK && (fflush(file) != 0 || fsync(fileno(file)) != 0))
    {
        err = ESP_FAIL;
    }
    if (fclose(file) != 0)
    {
        err = ESP_FAIL;
    }
    return err;
}

esp_err_t SdStorage::recover()
{
    ESP_LOGW(logTag, "Recovering storage: remounting card");
    esp_err_t err = card_.remount();
    if (err != ESP_OK)
    {
        return err;
    }
    return ensureDirectory();
}
