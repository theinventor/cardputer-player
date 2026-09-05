#pragma once
#include "audio_player.h"
#include "order.h"
#include <M5Cardputer.h>
#include <Preferences.h>
#include <WiFi.h>
#include <cJSON.h>

namespace ct {
struct Settings {
    String ssid, password, token;
    bool wifi = false, visualizer = true;
    uint8_t brightness = 150, volume = 20;
    uint16_t sleepSeconds = 60;
};
class App {
public:
    Library library;
    AudioPlayer audio;
    Order order;
    Settings settings;
    Preferences preferences;
    Track currentTrack;
    String notice;
    bool ready = false;
    uint32_t keyboardEvents = 0, loopGapMs = 0;
    bool begin();
    void tick();
    bool play(int id, uint32_t position = 0, bool paused = false);
    bool control(const String& action, const String& value, String& error);
    bool rescan();
    void saveSettings();
    void saveResume();
    void connectWifi();
    void configureWifi(const String& ssid, const String& password);
    cJSON* status();
    cJSON* trackJson(int id);
    static void jsonPrint(cJSON* json);
private:
    uint32_t lastSave_ = 0, handledEnd_ = 0, lastWifiAttempt_ = 0, lastTick_ = 0;
    Playback lastPlayback_ = Playback::Stopped;
    bool wasConnected_ = false;
    String serialLine_;
    void serial();
};
String jsonString(cJSON* json);
bool parseNumber(const String& text, uint32_t maximum, uint32_t& value);
}
