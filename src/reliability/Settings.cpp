#include "reliability/Settings.hpp"

#include <string.h>

#include "esp_log.h"
#include "nvs.h"
#include "nvs_flash.h"

#include "config.hpp"
#include "periph/GnssProfile.hpp"

namespace
{
    constexpr const char *logTag = "settings";

    template <size_t N>
    void copyString(char (&dest)[N], const char *src)
    {
        strncpy(dest, src, N - 1);
        dest[N - 1] = '\0';
    }

    template <size_t N>
    bool terminate(char (&text)[N])
    {
        if (memchr(text, '\0', N) != nullptr)
        {
            return false;
        }
        text[N - 1] = '\0';
        return true;
    }

    bool clamp(uint32_t &value, uint32_t low, uint32_t high)
    {
        if (value < low)
        {
            value = low;
            return true;
        }
        if (value > high)
        {
            value = high;
            return true;
        }
        return false;
    }
}

void Settings::begin()
{
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND)
    {
        ESP_LOGW(logTag, "NVS partition unusable (%s), erasing", esp_err_to_name(err));
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }
    ESP_ERROR_CHECK(err);
}

Settings::Values Settings::defaults()
{
    Values values = {};
    values.version = layoutVersion;
    values.movingIntervalMs = Config::Defaults::movingIntervalMs;
    values.stationaryIntervalMs = Config::Defaults::stationaryIntervalMs;
    values.minDistanceM = Config::Defaults::minDistanceM;
    values.motionStartCmPerS = Config::Defaults::motionStartCmPerS;
    values.motionStopCmPerS = Config::Defaults::motionStopCmPerS;
    values.motionStopDelayMs = Config::Defaults::motionStopDelayMs;
    values.gnssProfile = Config::Defaults::gnssProfile;
    values.uplinkEnabled = Config::Defaults::uplinkEnabled;
    values.uplinkMinIntervalMs = Config::Defaults::uplinkMinIntervalMs;
    values.healthIntervalMs = Config::Defaults::healthIntervalMs;
    copyString(values.apn, Config::Defaults::apn);
    copyString(values.serverUrl, Config::Defaults::serverUrl);
    copyString(values.deviceId, Config::Defaults::deviceId);
    return values;
}

bool Settings::validate(Values &values)
{
    bool changed = false;
    changed |= clamp(values.movingIntervalMs, 1000, 3'600'000);
    changed |= clamp(values.stationaryIntervalMs, 1000, 86'400'000);
    changed |= clamp(values.minDistanceM, 0, 100'000);
    changed |= clamp(values.motionStartCmPerS, 10, 10'000);
    changed |= clamp(values.motionStopCmPerS, 0, values.motionStartCmPerS);
    changed |= clamp(values.motionStopDelayMs, 1000, 3'600'000);
    changed |= clamp(values.uplinkMinIntervalMs, 1000, 3'600'000);
    changed |= clamp(values.healthIntervalMs, 10'000, 86'400'000);
    if (values.gnssProfile >= GnssProfiles::count)
    {
        values.gnssProfile = Config::Defaults::gnssProfile;
        changed = true;
    }
    changed |= terminate(values.apn);
    changed |= terminate(values.serverUrl);
    changed |= terminate(values.deviceId);
    if (values.deviceId[0] == '\0')
    {
        copyString(values.deviceId, Config::Defaults::deviceId);
        changed = true;
    }
    return !changed;
}

bool Settings::load(Values &values) const
{
    values = defaults();

    nvs_handle_t handle = 0;
    if (nvs_open(Config::nvsNamespace, NVS_READONLY, &handle) != ESP_OK)
    {
        return false; // namespace does not exist until the first save
    }

    Values stored = {};
    size_t length = sizeof stored;
    const esp_err_t err = nvs_get_blob(handle, Config::nvsSettingsKey, &stored, &length);
    nvs_close(handle);

    if (err != ESP_OK || length != sizeof stored || stored.version != layoutVersion)
    {
        ESP_LOGW(logTag, "Stored settings unusable (%s, %u bytes, layout %" PRIu32 "), using defaults",
                 esp_err_to_name(err), static_cast<unsigned>(length), stored.version);
        return false;
    }

    if (!validate(stored))
    {
        ESP_LOGW(logTag, "Some stored settings were out of range and clamped");
    }
    values = stored;
    return true;
}

esp_err_t Settings::save(const Values &values) const
{
    Values checked = values;
    validate(checked);
    checked.version = layoutVersion;

    nvs_handle_t handle = 0;
    esp_err_t err = nvs_open(Config::nvsNamespace, NVS_READWRITE, &handle);
    if (err != ESP_OK)
    {
        return err;
    }
    err = nvs_set_blob(handle, Config::nvsSettingsKey, &checked, sizeof checked);
    if (err == ESP_OK)
    {
        err = nvs_commit(handle);
    }
    nvs_close(handle);
    return err;
}
