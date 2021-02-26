#include <WiFi.h>
#include "time.h"

#include "esp32-hal.h"
#include "lwip/apps/sntp.h"
#include "rom/crc.h"

#include "SPIFFS.h"
#include "esp_spiffs.h"

#include "task_scheduler.h"
#include "wifi_module.h"

#include "FFWLeo.h"
#include "teddyminator.h"


//TFT Setup
#include <TFT_eSPI.h> // Graphics and font library for ST7735 driver chip
#include <SPI.h>
TFT_eSPI tft = TFT_eSPI(); 




const char *ntpServer = "pool.ntp.org";
const long gmtOffset_sec = 0;
const int daylightOffset_sec = 3600;

bool mount_or_format_spiffs() {
    esp_vfs_spiffs_conf_t conf = {
      .base_path = "/spiffs",
      .partition_label = NULL,
      .max_files = 10,
      .format_if_mount_failed = false
    };

    esp_err_t err = esp_vfs_spiffs_register(&conf);
    if(err == ESP_FAIL) {
        Serial.println("SPIFFS is not formatted. Formatting now. This will take about 30 seconds.");
        SPIFFS.format();
    } else {
        esp_vfs_spiffs_unregister(NULL);
    }

    return SPIFFS.begin(false);
}

void printLocalTime()
{
    struct tm timeinfo;
    if (!getLocalTime(&timeinfo))
    {
        Serial.println("Failed to obtain time");
        return;
    }
    Serial.println(&timeinfo, "%A, %B %d %Y %H:%M:%S");
    Serial.print("Day of week: ");
    Serial.println(&timeinfo, "%A");
    Serial.print("Month: ");
    Serial.println(&timeinfo, "%B");
    Serial.print("Day of Month: ");
    Serial.println(&timeinfo, "%d");
    Serial.print("Year: ");
    Serial.println(&timeinfo, "%Y");
    Serial.print("Hour: ");
    Serial.println(&timeinfo, "%H");
    Serial.print("Hour (12 hour format): ");
    Serial.println(&timeinfo, "%I");
    Serial.print("Minute: ");
    Serial.println(&timeinfo, "%M");
    Serial.print("Second: ");
    Serial.println(&timeinfo, "%S");

    Serial.println("Time variables");
    char timeHour[3];
    strftime(timeHour, 3, "%H", &timeinfo);
    Serial.println(timeHour);
    char timeWeekDay[10];
    strftime(timeWeekDay, 10, "%A", &timeinfo);
    Serial.println(timeWeekDay);
    Serial.println();
}

TaskScheduler task_scheduler;
WifiModule wifi;

bool timesync_successful_once = false;

uint16_t last_minute = 0;
bool last_pulse = true;

uint16_t target = 0;

const int INA = 12;
const int INB = 13;
//const int INA = 4;
//const int INB = 32;
//const int INA = 18;
//const int INB = 20;
const int betweenPulses = 500;
const int pulseLength = 500;

void pulse_and_save() {
    // last_pulse flipped intentionally
    String content = String(last_minute, 10) + String(';') + String(last_pulse ? 'f' : 't') + String(';');
    uint32_t crc = crc32_le(0, (const uint8_t *)content.begin(), content.length());
    content += String(crc, 10);

    int to_pulse = last_pulse ? INA : INB;

    digitalWrite(to_pulse, HIGH);
    File file = SPIFFS.open("/time", "w");
    file.print(content);
    file.close();
    delay(pulseLength);
    digitalWrite(to_pulse, LOW);

    delay(betweenPulses);
    last_pulse = !last_pulse;
}

void read_time_from_flash() {
    File file = SPIFFS.open("/time", "r");
    String content = file.readString();
    Serial.println(content);
    String copy = content;
    file.close();

    //123;t;crc
    String minutes = "";
    while(content[0] != ';') {
        minutes += content[0];
        content.remove(0, 1);
    }
    Serial.println(minutes);
    Serial.println(content);
    content.remove(0, 1);

    bool pulse = content[0] == 't';
    content.remove(0, 1);
    content.remove(0, 1);

    Serial.println(pulse);
    Serial.println(content);

    uint32_t crc = strtoul(content.c_str(), nullptr, 10);
    uint32_t expected = crc32_le(0, (const uint8_t *)copy.begin(), minutes.length() + 3);
    if (crc != expected) {
        Serial.printf("CRC was %u but expected was %u", crc, expected);
        Serial.println();
        delay(0xFFFFFFFF);
        return;
    }

    last_minute = (uint32_t)minutes.toInt();
    last_pulse = pulse;
}

void sync_time() {
    configTime(gmtOffset_sec, daylightOffset_sec, ntpServer);
    setenv("TZ", "CET-1CEST,M3.5.0,M10.5.0/3", 1);
    tzset();
    struct tm timeinfo;
    bool timesync_successful = getLocalTime(&timeinfo);

    timesync_successful_once |= timesync_successful;

    if(timesync_successful) {
        task_scheduler.scheduleOnce("get_time", [](){
            sync_time();
        }, 3*60*60*1000);
    } else {
        task_scheduler.scheduleOnce("get_time", [](){
            sync_time();
        }, 10*1000);
    }
}

bool first_draw = true;

void setup()
{
    tft.init();
    tft.setRotation(0);
    tft.fillScreen(TFT_WHITE);
    tft.setSwapBytes(true);
    tft.pushImage(0, 0, teddyminator_width, teddyminator_height, teddyminator);
            
    Serial.begin(115200);

    pinMode(INA, OUTPUT);
    pinMode(INB, OUTPUT);
    
    

    if(!mount_or_format_spiffs()) {
        Serial.println("Failed to format SPIFFS.");
        delay(0xFFFFFFFF);
    }

    if (SPIFFS.exists("/time")) {
        read_time_from_flash();
    }

    task_scheduler.setup();
    wifi.setup();

    // Init and get the time
    task_scheduler.scheduleOnce("get_time", [](){
        sync_time();
    }, 0);

    task_scheduler.scheduleWithFixedDelay("set_target_time", [](){
        if (!timesync_successful_once)
            return;

        struct tm timeinfo;
        if (!getLocalTime(&timeinfo)) {
            Serial.println("blah");
            return;
        }

        auto new_target = timeinfo.tm_hour * 60 + timeinfo.tm_min;
        if (new_target != target) {
            Serial.printf("New target is %u", new_target);
            Serial.println();
        }
        target = new_target;
        //target = 3*60+16;
    }, 100, 100);

    task_scheduler.scheduleWithFixedDelay("pulse_clock", [](){
        if (!timesync_successful_once || target == last_minute) {
            delay(100);
            return;
        }
        if (target != last_minute) {
            last_minute = (last_minute + 1) % (24 * 60);
            Serial.printf("Tick %u", last_minute);
            Serial.println();
            pulse_and_save();
        }
    }, 100, 0);


    task_scheduler.scheduleWithFixedDelay("draw_tft", [](){
            if(!timesync_successful_once)
                return;
            struct tm timeinfo;
            getLocalTime(&timeinfo);

            if(first_draw) {
                tft.fillScreen(TFT_BLACK);
                first_draw = false;
            }
            char timeHour[6];
            strftime(timeHour, 6, "%H:%M", &timeinfo);
           
            tft.setTextColor(TFT_RED, TFT_BLACK);
            tft.drawString(timeHour,50,10,7);
            tft.setSwapBytes(true);
            tft.pushImage(95, 90, logo_width, logo_height, logo);
        }, 2000, 500);


}














void loop()
{
    task_scheduler.loop();
}
