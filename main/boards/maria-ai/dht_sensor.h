#pragma once

#include "config.h"
#include "dht11.h"
#include "mcp_server.h"

class DhtSensor {
public:
    explicit DhtSensor(gpio_num_t pin);

    void Start();   // pornește citirea
    void Stop();    // oprește citirea

    static void BackgroundTask(void* arg);   // <-- MUTAT ÎN PUBLIC

private:
    bool enabled_ = false;        // flag pentru controlul taskului
    xiaozhi::DHT11 dht_;
};
