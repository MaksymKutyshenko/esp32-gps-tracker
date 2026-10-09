#pragma once

#include <stdint.h>

#include "driver/gpio.h"
#include "driver/spi_master.h"
#include "esp_err.h"
#include "sdmmc_cmd.h"

// MicroSD card over SPI, mounted as a FAT volume. Owns the SPI bus (the
// card is its only device) and the mount state. Mounting is a runtime
// operation, not a boot-time one: a tracker must start without a card and
// pick one up later, and must survive the card being pulled mid-write —
// the logger calls remount() when writes start failing.
class SdCard
{
public:
    SdCard(spi_host_device_t host, gpio_num_t sclkPin, gpio_num_t mosiPin,
           gpio_num_t misoPin, gpio_num_t csPin, uint32_t clockHz,
           const char *mountPoint);

    // Initialises the SPI bus only; call mount() to bring up the card.
    void begin();

    esp_err_t mount();
    esp_err_t unmount();
    esp_err_t remount();

    bool isMounted() const { return card_ != nullptr; }
    const char *mountPoint() const { return mountPoint_; }

    // Card size in MiB, 0 when not mounted.
    uint32_t capacityMiB() const;

private:
    spi_host_device_t host_;
    gpio_num_t sclkPin_;
    gpio_num_t mosiPin_;
    gpio_num_t misoPin_;
    gpio_num_t csPin_;
    uint32_t clockHz_;
    const char *mountPoint_;
    sdmmc_card_t *card_ = nullptr;
};
