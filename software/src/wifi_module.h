#pragma once

#include <stdint.h>

#define MAX_CONNECT_ATTEMPT_INTERVAL_MS 5 * 60 * 1000

enum class WifiState {
    NOT_CONNECTED,
    CONNECTING,
    CONNECTED
};

class WifiModule {
public:
    WifiModule();
    void setup();

    bool initialized = false;

    WifiState state;

private:
    void apply_sta_config_and_connect();

    uint32_t connect_attempt_interval_ms;
};
