#pragma once

#include "periph/SdCard.hpp"
#include "transport/IStorage.hpp"

// IStorage on the FAT volume of the MicroSD card. Each append opens,
// writes, flushes and closes the file: slower than keeping it open, but a
// card pulled between two records loses at most the record in flight, and
// the file is always consistent for a PC.
class SdStorage final : public IStorage
{
public:
    SdStorage(SdCard &card, const char *directory);

    // Mounts the card and creates the track directory. A failure is logged
    // and reported, not fatal: the tracker runs without a card and retries
    // through recover().
    esp_err_t begin();

    bool isReady() const override { return card_.isMounted(); }
    esp_err_t append(const char *path, const char *header,
                     const char *line, size_t length) override;
    esp_err_t recover() override;

private:
    esp_err_t ensureDirectory();

    SdCard &card_;
    const char *directory_;
};
