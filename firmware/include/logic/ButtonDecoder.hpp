#pragma once

#include <stdint.h>

// Turns sampled button levels into presses. The caller samples the pin
// every few milliseconds while the button is active and feeds each
// sample with its timestamp; the decoder is the four-state debounce from
// the module 2.6 homework (released, confirming a press, pressed,
// confirming a release): a level has to hold for stableUs before it
// counts, so contact bounce of any length shorter than that is ignored.
//
// A press is reported as soon as it is confirmed, not at the release, so
// the screen reacts at once and a bouncing release can never count as a
// second press. Holding the button for longPressUs reports Long once more
// while it is still held.
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
    ButtonDecoder(int64_t stableUs, int64_t longPressUs)
        : stableUs_(stableUs), longPressUs_(longPressUs)
    {
    }

    ButtonPress feed(bool pressed, int64_t nowUs)
    {
        switch (state_)
        {
        case State::Released:
            if (pressed)
            {
                state_ = State::ConfirmingPress;
                sinceUs_ = nowUs;
            }
            return ButtonPress::None;

        case State::ConfirmingPress:
            if (!pressed)
            {
                state_ = State::Released;
            }
            else if (nowUs - sinceUs_ >= stableUs_)
            {
                state_ = State::Pressed;
                pressedAtUs_ = sinceUs_;
                longReported_ = false;
                return ButtonPress::Short;
            }
            return ButtonPress::None;

        case State::Pressed:
            if (!pressed)
            {
                state_ = State::ConfirmingRelease;
                sinceUs_ = nowUs;
            }
            else if (!longReported_ && nowUs - pressedAtUs_ >= longPressUs_)
            {
                longReported_ = true;
                return ButtonPress::Long;
            }
            return ButtonPress::None;

        case State::ConfirmingRelease:
            if (pressed)
            {
                state_ = State::Pressed;
            }
            else if (nowUs - sinceUs_ >= stableUs_)
            {
                state_ = State::Released;
            }
            return ButtonPress::None;
        }
        return ButtonPress::None;
    }

    // True while a press is being confirmed, held, or its release is being
    // confirmed: the caller keeps sampling until this goes false.
    bool active() const { return state_ != State::Released; }

private:
    enum class State : uint8_t
    {
        Released,
        ConfirmingPress,
        Pressed,
        ConfirmingRelease,
    };

    int64_t stableUs_;
    int64_t longPressUs_;
    State state_ = State::Released;
    int64_t sinceUs_ = 0;
    int64_t pressedAtUs_ = 0;
    bool longReported_ = false;
};
