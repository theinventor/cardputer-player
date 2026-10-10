#pragma once
#include "playlists.h"
#include <memory>

namespace ct {
// One emulator instance; callbacks run on the main loop, never the MP3 worker.
class GameBoy {
public:
    static constexpr const char* DefaultRom = "/Games/pokemon-red.gb";
    static constexpr uint32_t MaxRomBytes = 2 * 1024 * 1024;
    static constexpr unsigned SampleRate = 22050, Samples = 369 * 2;
    using DrawLine = void (*)(void*, const uint8_t*, unsigned);
    GameBoy();
    ~GameBoy();
    bool open(PlaylistFiles& files, const std::string& path);
    void close();
    bool active() const;
    bool step(unsigned instructions = 4096);
    void buttons(uint8_t pressed);
    void sound(int16_t* samples);
    void display(DrawLine draw, void* context);
    bool save();
    bool paused() const { return paused_; }
    void pause(bool value) { paused_ = value; }
    uint32_t frames() const;
    const std::string& error() const { return error_; }
    const std::string& saveError() const { return saveError_; }
    const std::string& title() const { return title_; }
    static bool validPath(const std::string& path);
    static bool validate(Reader& reader, std::string& error);
private:
    struct State;
    std::unique_ptr<State> state_;
    bool paused_ = false;
    std::string error_, saveError_, title_;
};
// Active-high GB buttons: A, B, Select, Start, Right, Left, Up, Down.
uint8_t gameBoyButton(const std::string& key);
// Draw a GB scanline into the existing 240x135 RGB332 screen buffer.
void gameBoyRenderLine(uint8_t* screen, const uint8_t* pixels, unsigned line);
}
