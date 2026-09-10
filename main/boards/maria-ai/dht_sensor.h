#pragma once

#include <driver/gpio.h>
#include "mcp_server.h"

class DhtSensor {
public:
    explicit DhtSensor(gpio_num_t pin);

    bool Read(float& temperature, float& humidity);

private:
    gpio_num_t pin_;
};
