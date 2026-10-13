#include "periph/SdCard.hpp"

#include "driver/sdspi_host.h"
#include "esp_log.h"
#include "esp_vfs_fat.h"

namespace
{
    constexpr const char *logTag = "sdcard";
}

SdCard::SdCard(spi_host_device_t host, gpio_num_t sclkPin, gpio_num_t mosiPin,
               gpio_num_t misoPin, gpio_num_t csPin, uint32_t clockHz,
               uint32_t commandTimeoutMs, const char *mountPoint)
    : host_(host), sclkPin_(sclkPin), mosiPin_(mosiPin), misoPin_(misoPin),
      csPin_(csPin), clockHz_(clockHz), commandTimeoutMs_(commandTimeoutMs),
      mountPoint_(mountPoint)
{
}

void SdCard::begin()
{
    spi_bus_config_t busConfig = {};
    busConfig.sclk_io_num = sclkPin_;
    busConfig.mosi_io_num = mosiPin_;
    busConfig.miso_io_num = misoPin_;
    busConfig.quadwp_io_num = -1;
    busConfig.quadhd_io_num = -1;
    // 512-byte sectors plus the FAT driver's own transfers; the sdspi host
    // requires DMA, which is why this bus is not shared with anything that
    // needs non-DMA stack buffers.
    busConfig.max_transfer_sz = 4096;
    ESP_ERROR_CHECK(spi_bus_initialize(host_, &busConfig, SPI_DMA_CH_AUTO));
}

esp_err_t SdCard::mount()
{
    if (card_ != nullptr)
    {
        return ESP_OK;
    }

    sdmmc_host_t host = SDSPI_HOST_DEFAULT();
    host.slot = host_;
    host.max_freq_khz = static_cast<int>(clockHz_ / 1000);
    // One bound for every command, in place of ESP-IDF's 5 s for writes:
    // a card that stops answering mid-write (a jumper wire bouncing in a
    // bag) then returns an error the logger counts and recovers from,
    // instead of holding the logger task past the 5 s task watchdog.
    host.command_timeout_ms = static_cast<int>(commandTimeoutMs_);

    sdspi_device_config_t slot = SDSPI_DEVICE_CONFIG_DEFAULT();
    slot.gpio_cs = csPin_;
    slot.host_id = host_;

    esp_vfs_fat_sdmmc_mount_config_t mountConfig = {};
    // Never format: a card that fails to mount is a card to inspect on a
    // PC, not one to wipe in the field.
    mountConfig.format_if_mount_failed = false;
    mountConfig.max_files = 2;
    mountConfig.allocation_unit_size = 16 * 1024;

    const esp_err_t err = esp_vfs_fat_sdspi_mount(mountPoint_, &host, &slot, &mountConfig, &card_);
    if (err != ESP_OK)
    {
        card_ = nullptr;
        ESP_LOGW(logTag, "Mount failed: %s", esp_err_to_name(err));
        return err;
    }

    ESP_LOGI(logTag, "Mounted %s: %" PRIu32 " MiB", mountPoint_, capacityMiB());
    return ESP_OK;
}

esp_err_t SdCard::unmount()
{
    if (card_ == nullptr)
    {
        return ESP_OK;
    }
    const esp_err_t err = esp_vfs_fat_sdcard_unmount(mountPoint_, card_);
    card_ = nullptr;
    return err;
}

esp_err_t SdCard::remount()
{
    // The unmount can fail if the card is gone; the mount that follows is
    // what decides whether storage is back.
    const esp_err_t unmountErr = unmount();
    if (unmountErr != ESP_OK)
    {
        ESP_LOGW(logTag, "Unmount during remount: %s", esp_err_to_name(unmountErr));
    }
    return mount();
}

uint32_t SdCard::capacityMiB() const
{
    if (card_ == nullptr)
    {
        return 0;
    }
    const uint64_t bytes = static_cast<uint64_t>(card_->csd.capacity) * card_->csd.sector_size;
    return static_cast<uint32_t>(bytes / (1024 * 1024));
}
