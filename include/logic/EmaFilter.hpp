#pragma once

#include <stdint.h>

// Exponential moving average in integer arithmetic:
//   filtered += (sample - filtered) * Numerator / Denominator
// Alpha = Numerator / Denominator, so EmaFilter<int32_t, 1, 5> is alpha 0.2:
// a step settles to 63 % after 1/alpha = 5 samples. The first sample seeds
// the filter so there is no start-up transient from zero.
//
// Used on speed and altitude before they reach the display, so the numbers
// do not flicker with every epoch of receiver noise. Pure logic, no ESP-IDF
// dependency, unit-tested on the host.
template <typename T, int32_t Numerator, int32_t Denominator>
class EmaFilter
{
    static_assert(Denominator > 0, "denominator must be positive");
    static_assert(Numerator > 0 && Numerator <= Denominator, "alpha must be in (0, 1]");

public:
    T push(T sample)
    {
        if (!seeded_)
        {
            filtered_ = sample;
            seeded_ = true;
            return filtered_;
        }
        const int64_t delta = static_cast<int64_t>(sample) - static_cast<int64_t>(filtered_);
        filtered_ = static_cast<T>(filtered_ + delta * Numerator / Denominator);
        return filtered_;
    }

    T value() const { return filtered_; }
    bool seeded() const { return seeded_; }

    void reset()
    {
        seeded_ = false;
        filtered_ = T{};
    }

private:
    T filtered_ = T{};
    bool seeded_ = false;
};
