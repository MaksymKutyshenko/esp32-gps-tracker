#pragma once

#include <stddef.h>
#include <stdint.h>

// Fixed-point parsing of the decimal ASCII fields NMEA uses ("4916.45",
// "0.034", "-12"). No strtod: the result is an integer scaled by 10^digits,
// so "4916.45" with 5 fraction digits becomes 491645000. Extra fraction
// digits are truncated, missing ones are zero-padded.
//
// Pure logic, no ESP-IDF dependency.
namespace DecimalParser
{
    // Returns false on an empty string or any character that is not part of
    // a plain decimal number; `out` is untouched then.
    inline bool parseScaled(const char *text, uint8_t fractionDigits, int64_t &out)
    {
        if (text == nullptr || *text == '\0')
        {
            return false;
        }

        bool negative = false;
        if (*text == '-')
        {
            negative = true;
            ++text;
        }
        else if (*text == '+')
        {
            ++text;
        }

        int64_t value = 0;
        bool sawDigit = false;
        for (; *text >= '0' && *text <= '9'; ++text)
        {
            value = value * 10 + (*text - '0');
            sawDigit = true;
        }

        uint8_t consumed = 0;
        if (*text == '.')
        {
            ++text;
            for (; *text >= '0' && *text <= '9'; ++text)
            {
                if (consumed < fractionDigits)
                {
                    value = value * 10 + (*text - '0');
                    ++consumed;
                }
                sawDigit = true;
            }
        }

        if (!sawDigit || *text != '\0')
        {
            return false;
        }

        for (; consumed < fractionDigits; ++consumed)
        {
            value *= 10;
        }

        out = negative ? -value : value;
        return true;
    }

    // Unsigned integer with an exact digit count, as in "hhmmss" or "ddmmyy".
    inline bool parseDigits(const char *text, uint8_t count, uint32_t &out)
    {
        uint32_t value = 0;
        for (uint8_t i = 0; i < count; ++i)
        {
            if (text[i] < '0' || text[i] > '9')
            {
                return false;
            }
            value = value * 10 + static_cast<uint32_t>(text[i] - '0');
        }
        out = value;
        return true;
    }
}
