#pragma once
#include "audio_player.h"
#include "order.h"
#include "games.h"
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
    SDPlaylistFiles playlistFiles{library};
    Playlists playlists{playlistFiles};
    Games games;
    GameStore gameStore{playlistFiles};
    AudioPlayer audio;
    Order order;
    Settings settings;
    Preferences preferences;
    Track currentTrack;
    String notice;
    bool ready = false;
    uint32_t keyboardEvents = 0, loopGapMs = 0;
    uint32_t activePlaylist = 0;
    String playlistName = "All music";
    bool begin();
    void tick();
    bool play(int id, uint32_t position = 0, bool paused = false, int entry = -1);
    bool control(const String& action, const String& value, String& error);
    bool rescan();
    bool startGame(GameId id, String& error);
    bool exitGame(String& error);
    void gameInput(GameKey key);
    void saveGames();
    void saveSettings();
    void saveResume();
    void connectWifi();
    void configureWifi(const String& ssid, const String& password);
    cJSON* status();
    cJSON* trackJson(int id);
    cJSON* playlistJson(uint32_t id, uint32_t offset = 0, uint32_t limit = 32);
    bool selectPlaylist(uint32_t id, bool start, String& error, int track = -1, int entry = -1);
    bool editPlaylist(const String& action, uint32_t& id, const String& value, uint32_t to, String& error);
    void invalidatePlaylistCache() { cachedPlaylist_ = 0; cachedIds_.clear(); }
    bool musicUploaded();
    static void jsonPrint(cJSON* json);
private:
    uint32_t lastSave_ = 0, handledEnd_ = 0, lastWifiAttempt_ = 0, lastTick_ = 0;
    Playback lastPlayback_ = Playback::Stopped;
    bool wasConnected_ = false;
    bool resumeAfterGame_ = false;
    uint32_t lastGameSave_ = 0;
    String serialLine_;
    uint32_t cachedPlaylist_ = 0, cachedAt_ = 0;
    std::vector<uint32_t> cachedIds_;
    std::vector<uint32_t> resolvePlaylist(const Playlist& playlist, bool keepMissing = true);
    void restorePlaylist();
    void serial();
};
String jsonString(cJSON* json);
bool parseNumber(const String& text, uint32_t maximum, uint32_t& value);
}
