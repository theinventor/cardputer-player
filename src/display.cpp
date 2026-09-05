#include "display.h"

namespace ct {
namespace {
constexpr uint32_t Background = 0x101513, Surface = 0x232a26, Muted = 0xa3aea5, Accent = 0xd2f36b, Pink = 0xf098ab;
String timeText(uint32_t ms) { char text[20]; snprintf(text, sizeof(text), "%lu:%02lu", ms / 60000, (ms / 1000) % 60); return text; }
}
bool Display::begin() {
    M5.Display.setRotation(1);
    canvas_.setColorDepth(8); cover_.setColorDepth(16);
    if (!canvas_.createSprite(240, 135) || !cover_.createSprite(56, 56)) return false;
    canvas_.setFont(&fonts::lgfxJapanGothic_12); canvas_.setTextWrap(false);
    lastInput_ = millis(); render(); return true;
}
void Display::text(const String& value, int x, int y, int width, uint32_t color) {
    String clipped = value;
    if (canvas_.textWidth(clipped) > width) {
        while (clipped.length() && canvas_.textWidth(clipped + "...") > width) {
            size_t n = clipped.length() - 1;
            while (n && (uint8_t(clipped[n]) & 0xc0) == 0x80) --n;
            clipped.remove(n);
        }
        clipped += "...";
    }
    canvas_.setTextColor(color); canvas_.drawString(clipped, x, y);
}
void Display::loadCover() {
    cover_.fillSprite(Surface);
    cover_.fillCircle(28, 28, 21, 0x505e50);
    cover_.fillCircle(28, 28, 9, Accent);
    cover_.fillCircle(28, 28, 3, Background);
    if (!app_.currentTrack.path[0]) return;
    SDLock lock(sdMutex);
    if (!strcmp(app_.currentTrack.path, "/@demo.mp3")) {
        if (LittleFS.exists("/demo.jpg")) cover_.drawJpgFile(LittleFS, "/demo.jpg", 0, 0, 56, 56, 0, 0, 56.0f / 96);
    } else {
        String path = app_.currentTrack.path;
        path = path.substring(0, path.lastIndexOf('/') + 1) + "cover.jpg";
        if (SD.exists(path)) cover_.drawJpgFile(SD, path.c_str(), 0, 0, 56, 56, 0, 0, 0, 0);
    }
}
void Display::nowPlaying(const AudioState& state) {
    if (state.track != coverTrack_) { coverTrack_ = state.track; loadCover(); }
    cover_.pushSprite(&canvas_, 8, 40);
    String title = app_.currentTrack.title[0] ? app_.currentTrack.title : "No track selected";
    String first, rest = title;
    while (rest.length() && canvas_.textWidth(first + rest[0]) < 156) { first += rest[0]; rest.remove(0, 1); }
    text(first, 72, 40, 160); text(rest, 72, 54, 160);
    text(app_.currentTrack.artist, 72, 72, 160, Muted);
    String stateText = playbackName(state.playback);
    if (state.playback == Playback::Loading) stateText += " " + String(state.loadingPercent) + "%";
    if (app_.order.shuffled()) stateText += " / mix";
    if (app_.order.repeat() == Repeat::One) stateText += " / repeat 1";
    text(stateText, 72, 87, 160, Accent);
    for (int i = 0; i < 16; ++i) {
        int height = state.playback == Playback::Playing && app_.settings.visualizer ? 2 + state.bands[i] * 18 / 100 : 2;
        canvas_.fillRect(8 + i * 9, 123 - height, 6, height, i < 11 ? Accent : Pink);
    }
    text(timeText(state.positionMs), 165, 101, 68);
    text(timeText(state.durationMs), 165, 115, 68, Muted);
    canvas_.fillRect(8, 130, 224, 3, Surface);
    if (state.durationMs) canvas_.fillRect(8, 130, uint64_t(std::min(state.positionMs, state.durationMs)) * 224 / state.durationMs, 3, Accent);
    if (state.error[0]) { canvas_.fillRect(0, 99, 240, 29, Background); text(state.error, 8, 106, 224, Pink); }
}
void Display::library() {
    text(searching_ ? "/ " + query_ + "_" : query_.isEmpty() ? String(app_.library.count()) + " tracks" : "/ " + query_, 8, 38, 224, Muted);
    int id = selected_;
    if (id < 0) { text("No matching music", 8, 65, 224); return; }
    for (int row = 0; row < 5 && id >= 0; ++row) {
        Track track;
        if (!app_.library.get(id, track)) break;
        int y = 53 + row * 15;
        if (!row) canvas_.fillRect(4, y, 232, 15, Accent);
        text(track.title, 8, y + 1, 223, !row ? Background : 0xf2f4f1);
        id = app_.library.find(query_.c_str(), id + 1);
    }
}
void Display::settings() {
    if (pairing_) {
        text("Access key", 8, 42, 225, Accent);
        text(app_.settings.token.substring(0, 16), 8, 64, 224);
        text(app_.settings.token.substring(16), 8, 80, 224);
        text(WiFi.isConnected() ? "http://" + WiFi.localIP().toString() : "Wi-Fi disconnected", 8, 108, 224, Muted);
        return;
    }
    if (editing_) {
        text(editing_ == 1 ? "Network name" : "Network password", 8, 45, 224, Accent);
        String value = input_;
        if (editing_ == 2) { value = ""; for (size_t i = 0; i < input_.length(); ++i) value += '*'; }
        while (canvas_.textWidth(value + "_") > 220) value.remove(0, 1);
        text(value + "_", 8, 75, 224);
        return;
    }
    String rows[] = {
        "Wi-Fi         " + String(app_.settings.wifi ? "on" : "off"),
        "Network       " + app_.settings.ssid,
        "Password      ********",
        WiFi.isConnected() ? WiFi.localIP().toString() : "Connect",
        "Access key",
        "Brightness    " + String(app_.settings.brightness * 100 / 255) + "%",
        "Screen sleep  " + String(app_.settings.sleepSeconds) + "s",
        "Visualizer    " + String(app_.settings.visualizer ? "on" : "off"),
        "Rescan music",
        "Speaker test"
    };
    int start = std::max(0, setting_ - 5);
    for (int i = start; i < std::min(10, start + 6); ++i) {
        int y = 40 + (i - start) * 15;
        if (i == setting_) canvas_.fillRect(4, y, 232, 15, Accent);
        text(rows[i], 8, y + 1, 224, i == setting_ ? Background : 0xf2f4f1);
    }
}
void Display::render() {
    auto state = app_.audio.state();
    canvas_.fillSprite(Background);
    canvas_.fillRect(0, 0, 240, 18, Surface);
    text("CARDTUNES", 7, 2, 82, Accent);
    text("V" + String(app_.settings.volume), 112, 2, 37, Muted);
    text(WiFi.isConnected() ? "WiFi" : "", 158, 2, 29, Muted);
    text(String(M5.Power.getBatteryLevel()) + "%", 198, 2, 40, Muted);
    const char* tabs[] = {"Playing", "Library", "Settings"};
    for (int i = 0; i < 3; ++i) {
        text(tabs[i], 8 + i * 80, 22, 74, i == view_ ? Accent : Muted);
        if (i == view_) canvas_.fillRect(8 + i * 80, 35, 54, 1, Accent);
    }
    if (view_ == 0) nowPlaying(state);
    else if (view_ == 1) library();
    else settings();
    canvas_.pushSprite(0, 0); lastDraw_ = millis();
}
void Display::command(const String& action, const String& value) {
    String error;
    if (!app_.control(action, value, error)) app_.notice = error.isEmpty() ? "Command unavailable" : error;
}
void Display::activateSetting(int delta) {
    switch (setting_) {
    case 0:
        app_.settings.wifi = !app_.settings.wifi;
        if (app_.settings.wifi) app_.connectWifi(); else WiFi.mode(WIFI_OFF);
        break;
    case 1: editing_ = 1; input_ = app_.settings.ssid; return;
    case 2: editing_ = 2; input_ = ""; return;
    case 3: app_.settings.wifi = true; app_.connectWifi(); break;
    case 4: pairing_ = true; return;
    case 5: app_.settings.brightness = std::max(20, std::min(255, int(app_.settings.brightness) + (delta < 0 ? -20 : 20))); M5.Display.setBrightness(app_.settings.brightness); break;
    case 6: app_.settings.sleepSeconds = app_.settings.sleepSeconds >= 120 ? 30 : app_.settings.sleepSeconds * 2; break;
    case 7: app_.settings.visualizer = !app_.settings.visualizer; break;
    case 8: app_.rescan(); selected_ = app_.library.count() ? 0 : -1; return;
    case 9: M5.Speaker.tone(660, 150, 1); return;
    }
    app_.saveSettings();
}
void Display::key(const String& key) {
    lastInput_ = millis();
    if (key == "lock") { locked_ = !locked_; sleeping_ = locked_; M5.Display.setBrightness(locked_ ? 0 : app_.settings.brightness); return; }
    if (locked_) return;
    if (sleeping_) { sleeping_ = false; M5.Display.setBrightness(app_.settings.brightness); }
    if (editing_) {
        if (key == "escape") { editing_ = 0; input_ = ""; }
        else if (key == "enter") {
            if (editing_ == 1) app_.settings.ssid = input_; else app_.settings.password = input_;
            app_.saveSettings(); editing_ = 0; input_ = "";
        } else if (key == "backspace") { if (input_.length()) input_.remove(input_.length() - 1); }
        else if (key.length() == 1 && input_.length() < (editing_ == 1 ? 32 : 63)) input_ += key;
    } else if (pairing_) { if (key == "escape" || key == "enter") pairing_ = false; }
    else if (searching_) {
        if (key == "escape" || key == "enter") searching_ = false;
        else if (key == "backspace") { if (query_.length()) query_.remove(query_.length() - 1); }
        else if (key.length() == 1 && query_.length() < 64) query_ += key;
        selected_ = app_.library.find(query_.c_str(), 0);
    } else if (key == "tab") view_ = (view_ + 1) % 3;
    else if (key == "escape" || key == "`") { view_ = 0; query_ = ""; }
    else if (key == "[" || key == "]") command("volume", String(std::max(0, std::min(100, int(app_.settings.volume) + (key == "[" ? -5 : 5)))));
    else if (view_ == 2) {
        if (key == "up" || key == ";") setting_ = std::max(0, setting_ - 1);
        else if (key == "down" || key == ".") setting_ = std::min(9, setting_ + 1);
        else if (key == "enter" || key == "left" || key == "right") activateSetting(key == "left" ? -1 : 1);
    } else if (key == " ") command("toggle");
    else if (key == "n") command("next");
    else if (key == "b") command("previous");
    else if (key == "s") command("shuffle", app_.order.shuffled() ? "off" : "on");
    else if (key == "r") command("repeat", app_.order.repeat() == Repeat::Off ? "all" : app_.order.repeat() == Repeat::All ? "one" : "off");
    else if (key == "v") { app_.settings.visualizer = !app_.settings.visualizer; app_.saveSettings(); }
    else if (key == "f") { view_ = 1; searching_ = true; query_ = ""; selected_ = 0; }
    else if (key == "left" || key == "right" || key == "," || key == "/") {
        int second = app_.audio.state().positionMs / 1000;
        command("seek", String(std::max(0, second + (key == "left" || key == "," ? -10 : 10))));
    } else if (view_ == 1) {
        if (key == "up" || key == ";" || key == "down" || key == ".") {
            int direction = key == "up" || key == ";" ? -1 : 1;
            int next = app_.library.find(query_.c_str(), selected_ + direction, direction);
            if (next >= 0) selected_ = next;
        } else if (key == "enter" && selected_ >= 0) { app_.play(selected_); view_ = 0; }
        else if (key == "q" && selected_ >= 0) command("enqueue", String(selected_));
    }
    render();
}
void Display::tick() {
    auto& keyboard = M5Cardputer.Keyboard;
    uint64_t held = 0;
    for (auto point : keyboard.keyList()) {
        if (point.x >= 0 && point.x < 14 && point.y >= 0 && point.y < 4) held |= uint64_t(1) << (point.y * 14 + point.x);
    }
    // The library's isChange() compares key counts, not press/release edges.
    // Dispatch each new physical key once, even when other keys remain held.
    uint64_t pressed = keys_.press(held);
    for (auto point : keyboard.keyList()) {
        if (point.x < 0 || point.x >= 14 || point.y < 0 || point.y >= 4 || !(pressed & (uint64_t(1) << (point.y * 14 + point.x)))) continue;
        ++app_.keyboardEvents;
        auto base = keyboard.getKeyValue(point).value_first;
        if (base == KEY_TAB) key("tab");
        else if (base == KEY_ENTER) key("enter");
        else if (base == KEY_BACKSPACE) key("backspace");
        else if (base != KEY_FN && base != KEY_OPT && base != KEY_LEFT_CTRL && base != KEY_LEFT_SHIFT && base != KEY_LEFT_ALT) {
            char c = keyboard.getKey(point);
            bool fn = keyboard.keysState().fn;
            if (fn && c == ';') key("up"); else if (fn && c == '.') key("down");
            else if (fn && c == ',') key("left"); else if (fn && c == '/') key("right");
            else if (fn && c == '`') key("escape"); else key(String(c));
        }
    }
    if (M5.BtnA.wasHold()) { clicks_ = 0; key("lock"); }
    if (M5.BtnA.wasClicked()) { ++clicks_; lastClick_ = millis(); }
    if (clicks_ && millis() - lastClick_ > 350) { command(clicks_ == 1 ? "toggle" : clicks_ == 2 ? "next" : "previous"); clicks_ = 0; }
    if (!sleeping_ && millis() - lastInput_ > uint32_t(app_.settings.sleepSeconds) * 1000) { sleeping_ = true; M5.Display.setBrightness(0); }
    if (!sleeping_ && millis() - lastDraw_ >= 100) render();
}
}
