#pragma once

#include <stdint.h>

#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

// RAII guard for a FreeRTOS mutex. Taking the lock and giving it back are
// tied to the guard's lifetime, so every early return and error path in a
// transfer releases the bus — a mutex held across an error is the classic
// way to deadlock the display and the sensor on the same I2C bus.
//
// The take has a timeout, never portMAX_DELAY: a bus that stays busy longer
// than this is a fault to report, not a reason to park the task forever.
//
//   BusLock lock(i2cMutex, Config::busLockTimeoutMs);
//   if (!lock.locked()) { ++diagnostics.busTimeouts; return ESP_ERR_TIMEOUT; }
//   ... transfer ...
class BusLock
{
public:
    BusLock(SemaphoreHandle_t mutex, uint32_t timeoutMs)
        : mutex_(mutex),
          locked_(xSemaphoreTake(mutex, pdMS_TO_TICKS(timeoutMs)) == pdTRUE)
    {
    }

    ~BusLock()
    {
        if (locked_)
        {
            xSemaphoreGive(mutex_);
        }
    }

    BusLock(const BusLock &) = delete;
    BusLock &operator=(const BusLock &) = delete;

    bool locked() const { return locked_; }

private:
    SemaphoreHandle_t mutex_;
    bool locked_;
};
