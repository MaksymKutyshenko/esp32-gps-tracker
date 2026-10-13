#pragma once

#include <stdint.h>

#include <atomic>

#include "driver/gpio.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"

// Active-low push button that reports every edge through a queue from its
// GPIO interrupt. The ISR does nothing but time-stamp the level and hand it
// over with xQueueSendFromISR; debouncing and press-length classification
// happen in the ui task through ButtonDecoder, where they can be tested.
//
// Button edges are events, not a polled level: the ui task sleeps until
// something actually happens.
struct ButtonEdge
{
    bool pressed;
    int64_t timestampUs;
};

class Button
{
public:
    explicit Button(gpio_num_t pin);

    void begin(QueueHandle_t edgeQueue);

    bool isPressed() const;

    // Edges the ISR saw, and those the queue could not take; read from
    // the ui task. A press that reaches nothing shows up here first: no
    // edges at all means the pin never moved (wiring), edges without a
    // decoded press means the decoder or the levels.
    uint32_t edges() const { return edges_.load(std::memory_order_relaxed); }
    uint32_t droppedEdges() const { return droppedEdges_.load(std::memory_order_relaxed); }

private:
    static void isrEntry(void *self);

    gpio_num_t pin_;
    QueueHandle_t queue_ = nullptr;
    std::atomic<uint32_t> edges_{0};
    std::atomic<uint32_t> droppedEdges_{0};
};
