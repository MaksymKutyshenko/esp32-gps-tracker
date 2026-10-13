#pragma once

#include <stdint.h>

// Turns raw button edges (from the GPIO interrupt) into presses. Contact
// bounce shows up as edges a few hundred microseconds apart; any edge
// closer than debounceUs to the previous accepted edge is ignored. A
// release after at least longPressUs is a long press, otherwise short.
//
// Pure logic, no ESP-IDF dependency; unit-tested on the host.
enum class ButtonPress : uint8_t
{
    None,
    Short,
    Long,
};

class ButtonDecoder
{
public:
    ButtonDecoder(int64_t debounceUs, int64_t longPressUs)
        : debounceUs_(debounceUs), longPressUs_(longPressUs)
    {
    }

    ButtonPress feed(bool pressed, int64_t timestampUs)
    {
        if (haveEdge_ && timestampUs - lastEdgeUs_ < debounceUs_)
        {
            return ButtonPress::None;
        }
        if (pressed == pressed_)
        {
            return ButtonPress::None; // repeated level, e.g. after a dropped edge
        }

        lastEdgeUs_ = timestampUs;
        haveEdge_ = true;
        pressed_ = pressed;

        if (pressed)
        {
            pressedAtUs_ = timestampUs;
            return ButtonPress::None;
        }
        return (timestampUs - pressedAtUs_ >= longPressUs_) ? ButtonPress::Long
                                                             : ButtonPress::Short;
    }

    bool isPressed() const { return pressed_; }

private:
    int64_t debounceUs_;
    int64_t longPressUs_;
    int64_t lastEdgeUs_ = 0;
    int64_t pressedAtUs_ = 0;
    bool haveEdge_ = false;
    bool pressed_ = false;
};
