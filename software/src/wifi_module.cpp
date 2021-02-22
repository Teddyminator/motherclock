#include "wifi_module.h"

#include <WiFi.h>

#include <esp_wifi.h>

#include "task_scheduler.h"

#define RECONNECT_TIMEOUT_MS 30000

extern TaskScheduler task_scheduler;

WifiModule::WifiModule() {

}

void WifiModule::apply_sta_config_and_connect() {
    if(this->state == WifiState::CONNECTED) {
        return;
    }

    this->state = WifiState::CONNECTING;

    WiFi.persistent(false);
    WiFi.setAutoReconnect(false);
    WiFi.disconnect(false, true);

#include "wifi_passphrase.inc"

    WiFi.setHostname("esp-zentraluhr");
    Serial.println(String("Connecting to ") + ssid);
    WiFi.begin(ssid.c_str(), passphrase.c_str());
}

void WifiModule::setup()
{
    WiFi.persistent(false);

    WiFi.onEvent([this](WiFiEvent_t event, WiFiEventInfo_t info) {
            if(this->state == WifiState::CONNECTING) {
                Serial.println("Failed to connect.");
            } else if (this->state == WifiState::CONNECTED) {
                Serial.println("Disconnected.");
            }

            this->state = WifiState::NOT_CONNECTED;

            connect_attempt_interval_ms = std::min(connect_attempt_interval_ms * 2, (uint32_t)MAX_CONNECT_ATTEMPT_INTERVAL_MS);

            Serial.printf(" next attempt in %u seconds.", connect_attempt_interval_ms / 1000);
            Serial.println();

            task_scheduler.scheduleOnce("wifi_connect", [this](){
                apply_sta_config_and_connect();
            }, connect_attempt_interval_ms);
        },
        SYSTEM_EVENT_STA_DISCONNECTED);

    WiFi.onEvent([this](WiFiEvent_t event, WiFiEventInfo_t info) {
            this->state = WifiState::CONNECTED;

            Serial.println("Connected");
            connect_attempt_interval_ms = 5000;
        },
        SYSTEM_EVENT_STA_CONNECTED);

    WiFi.onEvent([this](WiFiEvent_t event, WiFiEventInfo_t info) {
            auto ip = WiFi.localIP();
            Serial.printf("Got IP address: %u.%u.%u.%u", ip[0], ip[1], ip[2], ip[3]);
            Serial.println();
        },
        SYSTEM_EVENT_STA_GOT_IP);

    WiFi.onEvent([this](WiFiEvent_t event, WiFiEventInfo_t info) {
        if(this->state != WifiState::CONNECTED)
            return;
        Serial.println("Lost IP. Forcing disconnect and reconnect of WiFi");
        this->state = WifiState::NOT_CONNECTED;
        WiFi.disconnect(false, true);
    }, SYSTEM_EVENT_STA_LOST_IP);

    connect_attempt_interval_ms = 5000;

    WiFi.mode(WIFI_STA);

    wifi_country_t config;
    config.cc[0] = 'D';
    config.cc[1] = 'E';
    config.cc[2] = ' ';
    config.schan = 1;
    config.nchan = 13;
    config.policy = WIFI_COUNTRY_POLICY_AUTO;
    esp_wifi_set_country(&config);

    esp_wifi_set_ps(WIFI_PS_NONE);

    WiFi.setTxPower(WIFI_POWER_19_5dBm);

    apply_sta_config_and_connect();
    initialized = true;
}
