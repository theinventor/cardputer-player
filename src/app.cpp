#include "app.h"
#include <ESPmDNS.h>

namespace ct {
String jsonString(cJSON* json) {
    char* raw = cJSON_PrintUnformatted(json);
    String result = raw ? raw : "{}";
    cJSON_free(raw); cJSON_Delete(json); return result;
}
bool parseNumber(const String& text, uint32_t maximum, uint32_t& value) {
    if (text.isEmpty()) return false;
    uint64_t n = 0;
    for (size_t i = 0; i < text.length(); ++i) {
        if (text[i] < '0' || text[i] > '9') return false;
        n = n * 10 + text[i] - '0'; if (n > maximum) return false;
    }
    value = n; return true;
}
bool App::begin() {
    preferences.begin("cardtunes", false);
    settings.ssid = preferences.getString("ssid");
    settings.password = preferences.getString("password");
    settings.token = preferences.getString("token");
    if (settings.token.isEmpty()) {
        uint8_t bytes[16]; esp_fill_random(bytes, sizeof(bytes));
        char token[33];
        for (size_t i = 0; i < sizeof(bytes); ++i) snprintf(token + i * 2, 3, "%02x", bytes[i]);
        settings.token = token; preferences.putString("token", settings.token);
    }
    settings.wifi = preferences.getBool("wifi", false);
    settings.volume = std::min<uint8_t>(preferences.getUChar("volume", 20), 100);
    settings.brightness = preferences.getUChar("brightness", 150);
    settings.visualizer = preferences.getBool("visualizer", true);
    settings.sleepSeconds = preferences.getUShort("sleep", 60);
    M5.Display.setBrightness(settings.brightness);
    library.begin();
    order.seed(esp_random()); order.reset(library.count());
    order.shuffle(preferences.getBool("shuffle", false));
    uint8_t repeat = preferences.getUChar("repeat", 1);
    order.repeat(repeat <= 2 ? static_cast<Repeat>(repeat) : Repeat::All);
    ready = audio.begin(settings.volume);
    audio.visualizer(settings.visualizer);
    if (!ready) notice = "Audio initialization failed";
    if (settings.wifi) connectWifi();
    String path = preferences.getString("track");
    int id = library.byPath(path.c_str());
    if (ready && id >= 0) play(id, preferences.getUInt("position", 0), true);
    Serial.printf("Cardtunes ready: board=%d sd=%d tracks=%lu heap=%lu\n", int(M5.getBoard()), library.mounted(), library.count(), ESP.getFreeHeap());
    return ready;
}
bool App::play(int id, uint32_t position, bool paused) {
    Track track;
    if (!ready || id < 0 || !library.get(id, track)) return false;
    AudioCommand cmd; cmd.action = AudioAction::Play; cmd.track = id;
    cmd.value = position; cmd.paused = paused; copyText(cmd.path, track.path);
    if (!audio.send(cmd)) return false;
    if (order.current() != id) order.select(id);
    currentTrack = track;
    return true;
}
bool App::control(const String& action, const String& value, String& error) {
    uint32_t n = 0;
    auto number = [&](uint32_t limit) { if (!parseNumber(value, limit, n)) { error = "Invalid numeric value"; return false; } return true; };
    AudioCommand cmd;
    if (action == "play") {
        if (value.isEmpty()) { auto state = audio.state(); if (state.playback == Playback::Paused) cmd.action = AudioAction::Resume; else return play(order.current() >= 0 ? order.current() : 0); }
        else { if (!number(library.count() ? library.count() - 1 : 0)) return false; return play(n); }
    } else if (action == "pause") cmd.action = AudioAction::Pause;
    else if (action == "toggle") {
        auto s = audio.state();
        if (s.playback == Playback::Stopped || s.playback == Playback::Ended || s.playback == Playback::Error) return play(order.current() >= 0 ? order.current() : 0);
        cmd.action = AudioAction::Toggle;
    } else if (action == "next") { int id = order.next(); if (id < 0) { error = "End of queue"; return false; } return play(id); }
    else if (action == "previous") {
        if (audio.state().positionMs > 3000) { cmd.action = AudioAction::Seek; cmd.value = 0; }
        else return play(order.previous());
    } else if (action == "stop") { saveResume(); cmd.action = AudioAction::Stop; }
    else if (action == "seek") { if (!number(UINT32_MAX / 1000)) return false; cmd.action = AudioAction::Seek; cmd.value = n * 1000; }
    else if (action == "volume") { if (!number(100)) return false; cmd.action = AudioAction::Volume; cmd.value = n; settings.volume = n; preferences.putUChar("volume", n); }
    else if (action == "shuffle") {
        if (value != "on" && value != "off") { error = "Expected on or off"; return false; }
        order.shuffle(value == "on"); preferences.putBool("shuffle", order.shuffled()); return true;
    } else if (action == "repeat") {
        if (value != "off" && value != "all" && value != "one") { error = "Expected off, all, or one"; return false; }
        order.repeat(value == "one" ? Repeat::One : value == "all" ? Repeat::All : Repeat::Off);
        preferences.putUChar("repeat", static_cast<uint8_t>(order.repeat())); return true;
    } else if (action == "enqueue") { if (!number(library.count() ? library.count() - 1 : 0)) return false; return order.enqueue(n); }
    else if (action == "clear-queue") { order.clearQueue(); return true; }
    else if (action == "rescan") return rescan();
    else { error = "Unknown action"; return false; }
    if (!audio.send(cmd)) { error = "Player is busy"; return false; }
    return true;
}
bool App::rescan() {
    if (!audio.stopAndWait()) return false;
    saveResume();
    notice = "Scanning music";
    bool ok = library.mounted() ? library.scan() : library.begin();
    order.reset(library.count()); currentTrack = Track{};
    notice = ok ? "Library updated" : "No microSD detected";
    return ok;
}
void App::saveSettings() {
    preferences.putBool("wifi", settings.wifi);
    preferences.putString("ssid", settings.ssid); preferences.putString("password", settings.password);
    preferences.putUChar("brightness", settings.brightness);
    preferences.putBool("visualizer", settings.visualizer);
    preferences.putUShort("sleep", settings.sleepSeconds);
    audio.visualizer(settings.visualizer);
}
void App::saveResume() {
    auto state = audio.state();
    if (state.track >= 0 && state.track == order.current() && state.playback != Playback::Loading && state.playback != Playback::Error && currentTrack.path[0]) {
        preferences.putString("track", currentTrack.path);
        preferences.putUInt("position", state.playback == Playback::Ended ? 0 : state.positionMs);
    }
    lastSave_ = millis();
}
void App::configureWifi(const String& ssid, const String& password) {
    settings.ssid = ssid; settings.password = password; settings.wifi = true;
    saveSettings(); connectWifi();
}
void App::connectWifi() {
    if (!settings.wifi || settings.ssid.isEmpty()) return;
    WiFi.mode(WIFI_STA); WiFi.setHostname("cardtunes");
    WiFi.begin(settings.ssid.c_str(), settings.password.c_str());
    lastWifiAttempt_ = millis();
}
cJSON* App::trackJson(int id) {
    Track track;
    if (id < 0 || !library.get(id, track)) return cJSON_CreateNull();
    auto object = cJSON_CreateObject();
    cJSON_AddNumberToObject(object, "id", id);
    cJSON_AddStringToObject(object, "path", track.path);
    cJSON_AddStringToObject(object, "title", track.title);
    cJSON_AddStringToObject(object, "artist", track.artist);
    cJSON_AddStringToObject(object, "album", track.album);
    return object;
}
cJSON* App::status() {
    auto s = audio.state();
    auto object = cJSON_CreateObject();
    cJSON_AddStringToObject(object, "name", "Cardtunes");
    cJSON_AddStringToObject(object, "version", "0.1.1");
    cJSON_AddStringToObject(object, "state", playbackName(s.playback));
    cJSON_AddItemToObject(object, "track", trackJson(s.track));
    cJSON_AddNumberToObject(object, "position_ms", s.positionMs);
    cJSON_AddNumberToObject(object, "duration_ms", s.durationMs);
    cJSON_AddNumberToObject(object, "volume", settings.volume);
    cJSON_AddBoolToObject(object, "shuffle", order.shuffled());
    cJSON_AddStringToObject(object, "repeat", order.repeat() == Repeat::One ? "one" : order.repeat() == Repeat::All ? "all" : "off");
    cJSON_AddBoolToObject(object, "sd_mounted", library.mounted());
    cJSON_AddNumberToObject(object, "tracks", library.count());
    cJSON_AddNumberToObject(object, "queue_count", order.queue().size());
    cJSON_AddNumberToObject(object, "heap_free", ESP.getFreeHeap());
    cJSON_AddNumberToObject(object, "heap_min", ESP.getMinFreeHeap());
    cJSON_AddNumberToObject(object, "uptime_ms", millis());
    cJSON_AddNumberToObject(object, "battery_percent", M5.Power.getBatteryLevel());
    cJSON_AddNumberToObject(object, "battery_mv", M5.Power.getBatteryVoltage());
    cJSON_AddStringToObject(object, "ip", WiFi.localIP().toString().c_str());
    cJSON_AddStringToObject(object, "ssid", WiFi.SSID().c_str());
    cJSON_AddNumberToObject(object, "rssi", WiFi.isConnected() ? WiFi.RSSI() : 0);
    cJSON_AddNumberToObject(object, "loading_percent", s.loadingPercent);
    cJSON_AddNumberToObject(object, "keyboard_keys", M5Cardputer.Keyboard.isPressed());
    cJSON_AddNumberToObject(object, "keyboard_events", keyboardEvents);
    cJSON_AddNumberToObject(object, "loop_gap_ms", loopGapMs);
    cJSON_AddNumberToObject(object, "keyboard_irq", digitalRead(11));
    cJSON_AddNumberToObject(object, "keyboard_pending", M5Cardputer.In_I2C.readRegister8(0x34, 0x03, 400000) & 15);
    cJSON_AddStringToObject(object, "error", s.error);
    cJSON_AddStringToObject(object, "notice", notice.c_str());
    auto bands = cJSON_AddArrayToObject(object, "bands");
    for (auto band : s.bands) cJSON_AddItemToArray(bands, cJSON_CreateNumber(band));
    return object;
}
void App::jsonPrint(cJSON* json) { Serial.println(jsonString(json)); }
void App::serial() {
    while (Serial.available()) {
        char c = Serial.read();
        if (c == '\n') {
            cJSON* json = cJSON_Parse(serialLine_.c_str()); serialLine_ = "";
            if (!json) continue;
            auto field = [&](const char* key) -> String { auto item = cJSON_GetObjectItemCaseSensitive(json, key); return cJSON_IsString(item) ? String(item->valuestring) : String(); };
            String cmd = field("cmd");
            if (cmd == "info") { auto response = status(); cJSON_AddStringToObject(response, "token", settings.token.c_str()); jsonPrint(response); }
            else if (cmd == "wifi" && !field("ssid").isEmpty() && field("ssid").length() <= 32 && field("password").length() <= 63) {
                configureWifi(field("ssid"), field("password")); Serial.println("{\"ok\":true}");
            } else if (cmd == "status") jsonPrint(status());
            else {
                String error;
                bool ok = control(cmd, field("value"), error);
                auto response = cJSON_CreateObject(); cJSON_AddBoolToObject(response, "ok", ok); cJSON_AddStringToObject(response, "error", error.c_str()); jsonPrint(response);
            }
            cJSON_Delete(json);
        } else if (c != '\r') {
            if (serialLine_.length() < 512) serialLine_ += c;
            else serialLine_ = "";
        }
    }
}
void App::tick() {
    uint32_t now = millis();
    if (lastTick_) loopGapMs = std::max(loopGapMs, now - lastTick_);
    lastTick_ = now;
    serial();
    auto state = audio.state();
    if (state.playback == Playback::Ended && state.epoch != handledEnd_) {
        handledEnd_ = state.epoch;
        saveResume(); int next = order.next(true); if (next >= 0) play(next);
    }
    if (state.playback != lastPlayback_ || millis() - lastSave_ > 30000) { saveResume(); lastPlayback_ = state.playback; }
    bool connected = WiFi.isConnected();
    if (connected && !wasConnected_) {
        MDNS.end(); MDNS.begin("cardtunes"); MDNS.addService("http", "tcp", 80);
        Serial.printf("Wi-Fi connected: %s, RSSI %d\n", WiFi.localIP().toString().c_str(), WiFi.RSSI());
    }
    wasConnected_ = connected;
    if (settings.wifi && !connected && millis() - lastWifiAttempt_ >= 30000) connectWifi();
}
}
