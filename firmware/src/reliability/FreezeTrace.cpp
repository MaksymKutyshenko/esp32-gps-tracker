#include "reliability/FreezeTrace.hpp"

#include <stddef.h>
#include <inttypes.h>
#include <stdint.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "esp_attr.h"
#include "esp_cpu.h"
#include "esp_freertos_hooks.h"
#include "esp_log.h"
#include "esp_private/system_internal.h"
#include "esp_timer.h"
#include "hal/systimer_ll.h"
#include "soc/systimer_struct.h"

// From the spi_flash component's private API (esp_private/cache_utils.h);
// IRAM-resident, which is the whole point here.
extern "C" bool spi_flash_cache_enabled(void);

namespace
{
    constexpr const char *logTag = "freeze";
    constexpr uint32_t traceMagic = 0x46545231; // "FTR1"
    constexpr size_t cores = 2;
    constexpr size_t depth = 16;           // samples kept per core
    constexpr uint32_t sampleEveryTicks = 250;
    constexpr size_t nameBytes = 8;

    struct Sample
    {
        int64_t us;          // esp_timer time of the sample
        uint32_t pc;         // EPC1: where the core was when the tick arrived
        uint32_t intenable;  // INTENABLE: which CPU interrupts were unmasked
        uint32_t cacheOn;    // flash cache enabled on this core
        uint32_t hookCalls;  // tick hook calls so far on this core
        uint32_t clockLow;   // systimer counter 0 (esp_timer), low word
        uint32_t tickLow;    // systimer counter 1 (OS tick), low word
        uint32_t period;     // this core's tick alarm period register
        uint32_t fired;      // this core's tick alarm interrupt flag
        char task[nameBytes];
    };

    struct Ring
    {
        uint32_t head;
        Sample samples[depth];
    };

    struct Trace
    {
        uint32_t magic;
        uint32_t storm; // stormMagic when the guard below reset the chip
        Ring rings[cores];
    };
    constexpr uint32_t stormMagic = 0x53544f52; // "STOR"

    // A tick storm: the tick interrupt firing hundreds of times per
    // millisecond, which starves every task. Between two samples the hook
    // runs sampleEveryTicks times; at the configured tick rate that is at
    // least this many systimer ticks apart. Seen closer together, the
    // chip is reset at once with the reason recorded, instead of sitting
    // frozen until the watchdog's 10 s hardware backstop.
    constexpr uint32_t ticksPerMs = 16000; // systimer at 16 MHz
    constexpr uint32_t stormBelowTicks = sampleEveryTicks * (1000 / configTICK_RATE_HZ) * ticksPerMs / 25;

    RTC_NOINIT_ATTR Trace trace;
    uint32_t ticks[cores];

    // Everything called here is IRAM-resident: this runs while the flash
    // cache may be off.
    void IRAM_ATTR sample(int core)
    {
        Ring &ring = trace.rings[core];
        Sample &s = ring.samples[ring.head % depth];
        uint32_t value = 0;
        asm volatile("rsr.epc1 %0" : "=r"(value));
        s.pc = value;
        asm volatile("rsr.intenable %0" : "=r"(value));
        s.intenable = value;
        s.us = esp_timer_get_time();
        s.cacheOn = spi_flash_cache_enabled() ? 1 : 0;
        s.hookCalls = ticks[core];
        systimer_ll_counter_snapshot(&SYSTIMER, 0);
        s.clockLow = systimer_ll_get_counter_value_low(&SYSTIMER, 0);
        systimer_ll_counter_snapshot(&SYSTIMER, 1);
        s.tickLow = systimer_ll_get_counter_value_low(&SYSTIMER, 1);
        s.period = systimer_ll_get_alarm_period(&SYSTIMER, static_cast<uint32_t>(core));
        s.fired = systimer_ll_is_alarm_int_fired(&SYSTIMER, static_cast<uint32_t>(core)) ? 1 : 0;

        if (ring.head > 0)
        {
            const Sample &previous = ring.samples[(ring.head - 1) % depth];
            if (s.clockLow - previous.clockLow < stormBelowTicks)
            {
                trace.storm = stormMagic;
                ring.head++;
                esp_restart_noos();
            }
        }
        const char *name = pcTaskGetName(xTaskGetCurrentTaskHandleForCore(core));
        size_t i = 0;
        for (; name != nullptr && i < nameBytes - 1 && name[i] != '\0'; ++i)
        {
            s.task[i] = name[i];
        }
        s.task[i] = '\0';
        ring.head++;
    }

    void IRAM_ATTR tickHook()
    {
        const int core = esp_cpu_get_core_id();
        if (++ticks[core] % sampleEveryTicks != 0)
        {
            return;
        }
        sample(core);
    }
}

bool FreezeTrace::tookStormReset()
{
    const bool storm = trace.magic == traceMagic && trace.storm == stormMagic;
    trace.storm = 0;
    return storm;
}

void FreezeTrace::logLastTrace(bool wanted)
{
    if (trace.magic == traceMagic && wanted)
    {
        int64_t newest = 0;
        for (size_t core = 0; core < cores; ++core)
        {
            const Ring &ring = trace.rings[core];
            for (size_t i = 0; i < depth; ++i)
            {
                if (ring.samples[i].us > newest) newest = ring.samples[i].us;
            }
        }
        ESP_LOGW(logTag, "Last samples before the reset (time before the newest sample):");
        for (size_t core = 0; core < cores; ++core)
        {
            const Ring &ring = trace.rings[core];
            const size_t count = ring.head < depth ? ring.head : depth;
            for (size_t n = 0; n < count; ++n)
            {
                const Sample &s = ring.samples[(ring.head - count + n) % depth];
                ESP_LOGW(logTag,
                         "core%u -%6.2fs pc 0x%08" PRIx32 " task %-7s cache %" PRIu32 " inten 0x%08" PRIx32
                         " hooks %" PRIu32 " clk %" PRIu32 " tick %" PRIu32 " period %" PRIu32 " fired %" PRIu32,
                         static_cast<unsigned>(core), static_cast<double>(newest - s.us) / 1e6, s.pc, s.task,
                         s.cacheOn, s.intenable, s.hookCalls, s.clockLow, s.tickLow, s.period, s.fired);
            }
        }
    }
    trace.magic = 0;
}

void FreezeTrace::begin()
{
    for (size_t core = 0; core < cores; ++core)
    {
        trace.rings[core].head = 0;
        ticks[core] = 0;
    }
    trace.storm = 0;
    trace.magic = traceMagic;
    ESP_ERROR_CHECK(esp_register_freertos_tick_hook_for_cpu(tickHook, 0));
    ESP_ERROR_CHECK(esp_register_freertos_tick_hook_for_cpu(tickHook, 1));
}
