#include "periph/Button.hpp"

#include "esp_attr.h"
#include "esp_timer.h"

Button::Button(gpio_num_t pin, bool pressedHigh) : pin_(pin), pressedLevel_(pressedHigh ? 1 : 0)
{
}

void Button::begin(QueueHandle_t edgeQueue)
{
    queue_ = edgeQueue;

    gpio_config_t config = {};
    config.pin_bit_mask = 1ULL << pin_;
    config.mode = GPIO_MODE_INPUT;
    config.pull_up_en = pressedLevel_ == 0 ? GPIO_PULLUP_ENABLE : GPIO_PULLUP_DISABLE;
    config.pull_down_en = pressedLevel_ == 0 ? GPIO_PULLDOWN_DISABLE : GPIO_PULLDOWN_ENABLE;
    config.intr_type = GPIO_INTR_ANYEDGE;
    ESP_ERROR_CHECK(gpio_config(&config));

    // The GPIO ISR service is installed once by App::begin for every pin
    // that raises interrupts (button, accelerometer).
    ESP_ERROR_CHECK(gpio_isr_handler_add(pin_, isrEntry, this));
}

bool Button::isPressed() const
{
    return gpio_get_level(pin_) == pressedLevel_;
}

void IRAM_ATTR Button::isrEntry(void *self)
{
    auto *button = static_cast<Button *>(self);
    ButtonEdge edge = {};
    edge.pressed = gpio_get_level(button->pin_) == button->pressedLevel_;
    edge.timestampUs = esp_timer_get_time();
    button->edges_.fetch_add(1, std::memory_order_relaxed);

    BaseType_t woken = pdFALSE;
    if (xQueueSendFromISR(button->queue_, &edge, &woken) != pdTRUE)
    {
        button->droppedEdges_.fetch_add(1, std::memory_order_relaxed);
    }
    portYIELD_FROM_ISR(woken);
}
