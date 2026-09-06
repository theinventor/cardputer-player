#pragma once
#include "app.h"
#include "input.h"

namespace ct {
class Display {
public:
    explicit Display(App& app) : app_(app), canvas_(&M5.Display), cover_(&M5.Display) {}
    bool begin();
    void tick();
    void render();
    void key(const String& key);
    M5Canvas& canvas() { return canvas_; }
private:
    App& app_;
    M5Canvas canvas_, cover_;
    KeyEdges keys_;
    int view_ = 0, selected_ = 0, setting_ = 0, coverTrack_ = -2;
    int playlistSelected_ = 0;
    int gameSelected_ = 0;
    GameId lastGame_ = GameId::None;
    uint32_t gameRevision_ = UINT32_MAX;
    String playlistError_;
    String gameError_;
    bool sleeping_ = false, locked_ = false, searching_ = false, pairing_ = false;
    int editing_ = 0;
    String input_, query_;
    uint32_t lastInput_ = 0, lastDraw_ = 0, lastClick_ = 0;
    unsigned clicks_ = 0;
    void text(const String& value, int x, int y, int width, uint32_t color = 0xf2f4f1);
    void nowPlaying(const AudioState& state);
    void library();
    void playlists();
    void gameMenu();
    void settings();
    void activateSetting(int delta = 0);
    void loadCover();
    void command(const String& action, const String& value = "");
};
}
