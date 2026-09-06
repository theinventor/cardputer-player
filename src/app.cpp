#include "app.h"
#include <ESPmDNS.h>
#include <esp_system.h>
#include <map>
#include <string_view>
#include <cstdlib>

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
    playlists.begin();
    std::srand(esp_random());
    gameStore.load(games);
    order.seed(esp_random()); order.reset(library.count());
    order.shuffle(preferences.getBool("shuffle", false));
    uint8_t repeat = preferences.getUChar("repeat", 1);
    order.repeat(repeat <= 2 ? static_cast<Repeat>(repeat) : Repeat::All);
    activePlaylist = preferences.getUInt("playlist", 0);
    restorePlaylist();
    ready = audio.begin(settings.volume);
    audio.visualizer(settings.visualizer);
    if (!ready) notice = "Audio initialization failed";
    if (settings.wifi) connectWifi();
    String path = preferences.getString("track");
    int id = library.byPath(path.c_str());
    if (ready && id >= 0 && order.contains(id)) play(id, preferences.getUInt("position", 0), true);
    Serial.printf("Cardtunes ready: board=%d sd=%d tracks=%lu heap=%lu\n", int(M5.getBoard()), library.mounted(), library.count(), ESP.getFreeHeap());
    return ready;
}
bool App::play(int id, uint32_t position, bool paused) {
    if (games.active() != GameId::None) return false;
    Track track;
    if (!ready || id < 0 || !order.contains(id) || !library.get(id, track)) return false;
    AudioCommand cmd; cmd.action = AudioAction::Play; cmd.track = id;
    cmd.value = position; cmd.paused = paused; copyText(cmd.path, track.path);
    if (!audio.send(cmd)) return false;
    if (order.current() != id) order.select(id);
    currentTrack = track;
    return true;
}
bool App::control(const String& action, const String& value, String& error) {
    if (action == "game") return startGame(gameId(value.c_str()), error);
    if (action == "game-exit") return exitGame(error);
    if (action == "game-key") {
        auto key = gameKey(value.c_str());
        if (games.active() == GameId::None || key == GameKey::None) { error = "No active game or invalid game key"; return false; }
        if (!games.input(key)) return exitGame(error);
        if (games.paused()) saveGames();
        return true;
    }
    if (games.active() != GameId::None && action != "volume") { error = "Exit the game before changing music"; return false; }
    uint32_t n = 0;
    auto number = [&](uint32_t limit) { if (!parseNumber(value, limit, n)) { error = "Invalid numeric value"; return false; } return true; };
    AudioCommand cmd;
    if (action == "playlist") {
        if (value != "all" && (!number(Playlists::MaxLists) || !n)) { error = "Expected playlist ID or all"; return false; }
        return selectPlaylist(value == "all" ? 0 : n, true, error);
    } else if (action == "play-library") {
        if (!number(library.count() ? library.count() - 1 : 0) || !library.count()) return false;
        if (activePlaylist && !selectPlaylist(0, false, error)) return false;
        return play(n);
    } else if (action == "play") {
        if (value.isEmpty()) { auto state = audio.state(); if (state.playback == Playback::Paused && order.current() == state.track) cmd.action = AudioAction::Resume; else return play(order.current() >= 0 ? order.current() : order.first()); }
        else { if (!number(library.count() ? library.count() - 1 : 0)) return false; return play(n); }
    } else if (action == "pause") cmd.action = AudioAction::Pause;
    else if (action == "toggle") {
        auto s = audio.state();
        if (s.playback == Playback::Stopped || s.playback == Playback::Ended || s.playback == Playback::Error) return play(order.current() >= 0 ? order.current() : order.first());
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
    } else if (action == "enqueue") {
        if (!number(library.count() ? library.count() - 1 : 0)) return false;
        if (!order.enqueue(n)) { error = "Queue full or track outside the active playlist"; return false; }
        return true;
    }
    else if (action == "clear-queue") { order.clearQueue(); return true; }
    else if (action == "rescan") return rescan();
    else { error = "Unknown action"; return false; }
    if (!audio.send(cmd)) { error = "Player is busy"; return false; }
    return true;
}
bool App::rescan() {
    if (games.active() != GameId::None) return false;
    if (!audio.stopAndWait()) return false;
    saveResume();
    notice = "Scanning music";
    bool ok = library.mounted() ? library.scan() : library.begin();
    if (library.mounted() && !gameStore.restored()) { Games card; if (gameStore.load(card)) games.mergeSaved(card); }
    order.reset(library.count()); currentTrack = Track{};
    playlists.begin(); restorePlaylist();
    notice = ok ? "Library updated" : "No microSD detected";
    return ok;
}
std::vector<uint32_t> App::resolvePlaylist(const Playlist& playlist) {
    std::map<std::string_view, int> wanted;
    for (const auto& path : playlist.paths) wanted.emplace(path, -1);
    Track track;
    for (uint32_t i = 0; i < library.count(); ++i) {
        if (library.get(i, track)) {
            auto found = wanted.find(track.path);
            if (found != wanted.end() && library.available(track.path)) found->second = i;
        }
        if (!(i % 32)) delay(1);
    }
    std::vector<uint32_t> ids;
    for (const auto& path : playlist.paths) if (wanted[path] >= 0) ids.push_back(wanted[path]);
    return ids;
}
void App::restorePlaylist() {
    if (!activePlaylist) { playlistName = "All music"; return; }
    Playlist playlist;
    if (playlists.load(activePlaylist, playlist)) {
        playlistName = playlist.name.c_str(); order.reset(resolvePlaylist(playlist));
    } else {
        playlistName = "Unavailable playlist"; order.reset(std::vector<uint32_t>{});
        notice = playlists.error().c_str();
    }
}
bool App::selectPlaylist(uint32_t id, bool start, String& error, int track) {
    if (games.active() != GameId::None) { error = "Exit the game before changing music"; return false; }
    Playlist playlist;
    std::vector<uint32_t> ids;
    if (id) {
        if (!playlists.load(id, playlist)) { error = playlists.error().c_str(); return false; }
        ids = resolvePlaylist(playlist);
        if (start && ids.empty()) { error = "Playlist has no available tracks"; return false; }
    } else if (start && !library.count()) { error = "Library is empty"; return false; }
    if (track >= 0 && (id ? std::find(ids.begin(), ids.end(), uint32_t(track)) == ids.end() : uint32_t(track) >= library.count())) {
        error = "Track is not available in this playlist"; return false;
    }
    if (!audio.stopAndWait()) { error = "Player is busy"; return false; }
    handledEnd_ = audio.state().epoch;
    if (id) order.reset(ids); else order.reset(library.count());
    activePlaylist = id; playlistName = id ? playlist.name.c_str() : "All music";
    currentTrack = Track{};
    preferences.putUInt("playlist", id); preferences.remove("track"); preferences.remove("position");
    if (start && !play(track >= 0 ? track : order.first())) { error = "Cannot start playlist"; return false; }
    saveResume();
    return true;
}
bool App::editPlaylist(const String& action, uint32_t& id, const String& value, uint32_t to, String& error) {
    if (games.active() != GameId::None) { error = "Exit the game before editing playlists"; return false; }
    if (action == "create") {
        if (playlists.create(value.c_str(), id)) return true;
        error = playlists.error().c_str(); return false;
    }
    Playlist playlist;
    if (!playlists.load(id, playlist)) { error = playlists.error().c_str(); return false; }
    if (action == "delete") {
        if (activePlaylist == id && !audio.stopAndWait()) { error = "Player is busy"; return false; }
        if (!playlists.erase(id)) { error = playlists.error().c_str(); return false; }
        if (activePlaylist == id) return selectPlaylist(0, false, error);
        return true;
    }
    uint32_t n = 0;
    if (action == "rename") playlist.name = value.c_str();
    else if (action == "add") {
        Track track;
        if (!parseNumber(value, Library::MaxTracks, n) || !library.get(n, track)) { error = "Track not found"; return false; }
        if (std::find(playlist.paths.begin(), playlist.paths.end(), track.path) != playlist.paths.end()) { error = "Track is already in this playlist"; return false; }
        if (playlist.paths.size() >= Playlists::MaxTracks) { error = "Playlist limit reached (128 tracks)"; return false; }
        playlist.paths.emplace_back(track.path);
    } else if (action == "remove" || action == "move") {
        if (playlist.paths.empty() || !parseNumber(value, playlist.paths.size() - 1, n) || (action == "move" && to >= playlist.paths.size())) { error = "Invalid playlist position"; return false; }
        std::string path = playlist.paths[n];
        playlist.paths.erase(playlist.paths.begin() + n);
        if (action == "move") playlist.paths.insert(playlist.paths.begin() + to, path);
    } else { error = "Unknown playlist action"; return false; }
    bool removingCurrent = activePlaylist == id && currentTrack.path[0] &&
        std::find(playlist.paths.begin(), playlist.paths.end(), currentTrack.path) == playlist.paths.end();
    if (removingCurrent && !audio.stopAndWait()) { error = "Player is busy"; return false; }
    if (!playlists.save(playlist)) { error = playlists.error().c_str(); return false; }
    if (activePlaylist == id) {
        playlistName = playlist.name.c_str();
        if (action != "rename") order.replace(resolvePlaylist(playlist));
        if (removingCurrent) { currentTrack = Track{}; preferences.remove("track"); preferences.remove("position"); }
    }
    return true;
}
cJSON* App::playlistJson(uint32_t id, uint32_t offset, uint32_t limit) {
    auto json = cJSON_CreateObject();
    cJSON_AddNumberToObject(json, "active_playlist_id", activePlaylist);
    if (!id) {
        auto lists = cJSON_AddArrayToObject(json, "playlists");
        for (const auto& item : playlists.list()) {
            auto entry = cJSON_CreateObject();
            cJSON_AddNumberToObject(entry, "id", item.id);
            cJSON_AddStringToObject(entry, "name", item.name.c_str());
            cJSON_AddNumberToObject(entry, "count", item.count);
            cJSON_AddItemToArray(lists, entry);
        }
    } else {
        Playlist playlist;
        if (!playlists.load(id, playlist)) { cJSON_Delete(json); return nullptr; }
        auto ids = resolvePlaylist(playlist);
        std::map<std::string, uint32_t> available;
        Track track;
        for (auto trackId : ids) if (library.get(trackId, track)) available[track.path] = trackId;
        cJSON_AddNumberToObject(json, "id", id);
        cJSON_AddStringToObject(json, "name", playlist.name.c_str());
        cJSON_AddNumberToObject(json, "count", playlist.paths.size());
        cJSON_AddNumberToObject(json, "available", ids.size());
        auto tracks = cJSON_AddArrayToObject(json, "tracks");
        uint32_t end = std::min<uint32_t>(playlist.paths.size(), offset + limit);
        for (uint32_t i = offset; i < end; ++i) {
            auto found = available.find(playlist.paths[i]);
            auto entry = found == available.end() ? cJSON_CreateObject() : trackJson(found->second);
            if (found == available.end()) {
                cJSON_AddNullToObject(entry, "id");
                cJSON_AddStringToObject(entry, "path", playlist.paths[i].c_str());
                cJSON_AddStringToObject(entry, "title", basename(playlist.paths[i]).c_str());
            }
            cJSON_AddBoolToObject(entry, "missing", found == available.end());
            cJSON_AddNumberToObject(entry, "position", i);
            cJSON_AddItemToArray(tracks, entry);
        }
        cJSON_AddNumberToObject(json, "next_offset", end < playlist.paths.size() ? int(end) : -1);
    }
    return json;
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
    cJSON_AddStringToObject(object, "version", "0.3.0");
    auto game = cJSON_AddObjectToObject(object, "game");
    cJSON_AddStringToObject(game, "id", gameSlug(games.active()));
    cJSON_AddStringToObject(game, "name", gameName(games.active()));
    cJSON_AddBoolToObject(game, "paused", games.paused());
    cJSON_AddBoolToObject(game, "over", games.over());
    cJSON_AddNumberToObject(game, "score", games.score(games.active()));
    cJSON_AddNumberToObject(game, "best", games.best(games.active()));
    cJSON_AddStringToObject(game, "save_error", gameStore.error().c_str());
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
    cJSON_AddNumberToObject(object, "active_playlist_id", activePlaylist);
    cJSON_AddStringToObject(object, "playlist_name", playlistName.c_str());
    cJSON_AddNumberToObject(object, "playlist_tracks", order.size());
    cJSON_AddNumberToObject(object, "heap_free", ESP.getFreeHeap());
    cJSON_AddNumberToObject(object, "heap_min", ESP.getMinFreeHeap());
    cJSON_AddNumberToObject(object, "uptime_ms", millis());
    cJSON_AddNumberToObject(object, "reset_reason", esp_reset_reason());
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
    bool wasPaused = games.paused();
    games.tick(lastTick_ ? now - lastTick_ : 0);
    if (games.active() != GameId::None && ((!wasPaused && games.paused()) || now - lastGameSave_ >= 30000)) saveGames();
    if (lastTick_) loopGapMs = std::max(loopGapMs, now - lastTick_);
    lastTick_ = now;
    serial();
    auto state = audio.state();
    if (games.active() == GameId::None && state.playback == Playback::Ended && state.epoch != handledEnd_) {
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
bool App::startGame(GameId id, String& error) {
    if (id == GameId::None) { error = "Expected blocks, breakout, or 2048"; return false; }
    if (games.active() != GameId::None) { error = "Exit the current game first"; return false; }
    auto before = audio.state();
    if (before.playback == Playback::Loading) { error = "Wait for the track to finish loading"; return false; }
    if (!audio.pauseAndWait(&resumeAfterGame_)) { error = "Player is busy; retry shortly"; return false; }
    saveResume(); games.start(id); lastGameSave_ = millis();
    return true;
}
bool App::exitGame(String& error) {
    if (games.active() == GameId::None) return true;
    if (resumeAfterGame_) {
        AudioCommand cmd; cmd.action = AudioAction::Resume;
        if (!audio.send(cmd)) { error = "Player is busy; retry exit"; return false; }
    }
    games.leave(); saveGames(); resumeAfterGame_ = false; return true;
}
void App::gameInput(GameKey key) {
    String error;
    if (!games.input(key) && !exitGame(error)) notice = error;
    if (games.paused()) saveGames();
}
void App::saveGames() { gameStore.save(games); lastGameSave_ = millis(); }
}
