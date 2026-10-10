#if defined(ARDUINO_ARCH_ESP32)
#pragma GCC optimize ("O3")
#endif
#include "gameboy.h"
#include <algorithm>
#include <array>
#include <cstring>
#include <new>

extern "C" {
#define MINIGB_APU_AUDIO_FORMAT_S16SYS
#define AUDIO_SAMPLE_RATE 22050
#include "third_party/peanut_gb/minigb_apu/minigb_apu.h"
}
static minigb_apu_ctx* currentApu = nullptr;
static uint8_t audio_read(uint16_t address) { return minigb_apu_audio_read(currentApu, address); }
static void audio_write(uint16_t address, uint8_t value) { minigb_apu_audio_write(currentApu, address, value); }
#define ENABLE_SOUND 1
#include "third_party/peanut_gb/peanut_gb.h"

namespace ct {
static_assert(GameBoy::Samples == AUDIO_SAMPLES_TOTAL);
namespace {
constexpr unsigned PageSize = 2048, PageCount = 8;
uint32_t hashBytes(uint32_t hash, const uint8_t* data, size_t count) {
    while (count--) { hash ^= *data++; hash *= 16777619; }
    return hash;
}
bool verifySave(PlaylistFiles& files, const std::string& path, uint32_t expected, size_t size) {
    auto file = files.openReader(path);
    if (!file || file->size() != size) return false;
    uint8_t block[512];
    uint32_t actual = 2166136261u;
    for (size_t at = 0; at < size;) {
        size_t count = std::min(sizeof(block), size - at);
        if (file->read(block, count) != count) return false;
        actual = hashBytes(actual, block, count); at += count;
    }
    return expected == actual;
}
}
struct GameBoy::State {
    gb_s gb{};
    minigb_apu_ctx apu{};
    GameBoy* owner;
    PlaylistFiles* files;
    std::unique_ptr<Reader> rom;
    const uint8_t* mappedRom = nullptr;
    std::unique_ptr<uint8_t[]> bank0, cache;
    // Four smaller allocations tolerate fragmentation left by Wi-Fi and MP3.
    static constexpr size_t RamPage = 8192;
    std::array<std::unique_ptr<uint8_t[]>, 4> ram;
    std::array<uint32_t, PageCount> tags{}, ages{};
    std::array<uint8_t, MaxRomBytes / PageSize> slots{};
    uint32_t age = 0, frames = 0;
    uint32_t cacheMisses = 0, romSize = 0;
    size_t ramSize = 0;
    std::string savePath;
    bool dirty = false, recovered = false, inFrame = false;
    DrawLine draw = nullptr;
    void* drawContext = nullptr;
    static State& self(gb_s* gb) { return *static_cast<State*>(gb->direct.priv); }
    static uint8_t readRom(gb_s* gb, uint_fast32_t address) {
        auto& s = self(gb);
        if (address >= s.romSize) { s.owner->error_ = "ROM address out of range"; return 0xff; }
        if (s.mappedRom) return s.mappedRom[address];
        if (address < 16384) return s.bank0[address];
        uint32_t page = address / PageSize;
        unsigned slot = s.slots[page];
        if (slot == 255) {
            ++s.cacheMisses;
            slot = std::min_element(s.ages.begin(), s.ages.end()) - s.ages.begin();
            if (!s.rom->seek(page * PageSize) || s.rom->read(s.cache.get() + slot * PageSize, PageSize) != PageSize) {
                s.owner->error_ = "ROM read failed; check SD card"; return 0xff;
            }
            if (s.tags[slot] != UINT32_MAX) s.slots[s.tags[slot]] = 255;
            s.tags[slot] = page; s.slots[page] = slot;
        }
        s.ages[slot] = ++s.age;
        return s.cache[slot * PageSize + address % PageSize];
    }
    static uint8_t readRam(gb_s* gb, uint_fast32_t address) {
        auto& s = self(gb); return address < s.ramSize ? s.ram[address / RamPage][address % RamPage] : 0xff;
    }
    static void writeRam(gb_s* gb, uint_fast32_t address, uint8_t value) {
        auto& s = self(gb);
        if (address < s.ramSize && s.ram[address / RamPage][address % RamPage] != value) {
            s.ram[address / RamPage][address % RamPage] = value; s.dirty = true;
        }
    }
    static void fail(gb_s* gb, gb_error_e, uint16_t) { self(gb).owner->error_ = "Game Boy emulation error"; }
    static void line(gb_s* gb, const uint8_t* pixels, uint_fast8_t y) {
        auto& s = self(gb); if (s.draw && y < 144) s.draw(s.drawContext, pixels, y);
    }
    bool load(const std::string& path) {
        auto file = files->openReader(path);
        if (!file || file->size() != ramSize) return false;
        for (size_t at = 0; at < ramSize; at += RamPage) {
            size_t count = std::min(RamPage, ramSize - at);
            if (file->read(ram[at / RamPage].get(), count) != count) return false;
        }
        return true;
    }
};
GameBoy::GameBoy() = default;
GameBoy::~GameBoy() = default;
bool GameBoy::validPath(const std::string& path) {
    if (path.size() < 11 || path.size() > 180 || path.compare(0, 7, "/Games/") || path.substr(path.size() - 3) != ".gb") return false;
    auto name = path.substr(7);
    if (name[0] == '.' || name.find("..") != std::string::npos) return false;
    for (unsigned char c : name) if (c < 32 || c > 126 || c == '/' || c == '\\' || c == ':') return false;
    return true;
}
bool GameBoy::validate(Reader& reader, std::string& error) {
    uint8_t header[0x150];
    if (!reader.seek(0) || reader.read(header, sizeof(header)) != sizeof(header)) { error = "Cannot read ROM header"; return false; }
    if (header[0x143] == 0xc0) { error = "Game Boy Color-only ROMs are not supported"; return false; }
    if (header[0x148] > 6 || reader.size() != (32768u << header[0x148])) { error = "Invalid ROM size (maximum 2 MiB)"; return false; }
    if (header[0x149] > 3) { error = "Cartridge RAM exceeds 32 KiB"; return false; }
    switch (header[0x147]) {
    case 0: case 1: case 2: case 3: case 5: case 6: case 8: case 9:
    case 0x11: case 0x12: case 0x13: case 0x19: case 0x1a: case 0x1b: break;
    default: error = "Unsupported cartridge (RTC and rumble not supported)"; return false;
    }
    uint8_t sum = 0;
    for (unsigned i = 0x134; i <= 0x14c; ++i) sum = sum - header[i] - 1;
    if (sum != header[0x14d]) { error = "ROM header checksum mismatch"; return false; }
    error.clear(); return true;
}
bool GameBoy::open(PlaylistFiles& files, const std::string& path, const uint8_t* mappedRom) {
    if (active()) { error_ = "Exit the current ROM first"; return false; }
    error_.clear(); saveError_.clear(); title_.clear(); paused_ = false;
    if (!validPath(path)) { error_ = "Expected /Games/name.gb"; return false; }
    auto rom = files.openReader(path);
    if (!rom) { error_ = "ROM not found on SD card"; return false; }
    if (!validate(*rom, error_)) return false;
    auto s = std::unique_ptr<State>(new (std::nothrow) State{});
    if (!s) { error_ = "Not enough memory for Game Boy"; return false; }
    s->owner = this; s->files = &files; s->rom = std::move(rom);
    s->romSize = s->rom->size();
    s->mappedRom = mappedRom;
    if (!mappedRom) {
        s->bank0.reset(new (std::nothrow) uint8_t[16384]);
        s->cache.reset(new (std::nothrow) uint8_t[PageCount * PageSize]);
        if (!s->bank0 || !s->cache) { error_ = "Not enough memory for ROM cache"; return false; }
        if (!s->rom->seek(0) || s->rom->read(s->bank0.get(), 16384) != 16384) { error_ = "ROM read failed"; return false; }
    }
    s->tags.fill(UINT32_MAX);
    s->slots.fill(255);
    minigb_apu_audio_init(&s->apu); currentApu = &s->apu;
    if (gb_init(&s->gb, State::readRom, State::readRam, State::writeRam, State::fail, s.get()) != GB_INIT_NO_ERROR) {
        error_ = "Unsupported ROM"; return false;
    }
    gb_get_save_size_s(&s->gb, &s->ramSize);
    if (s->ramSize) {
        for (size_t at = 0; at < s->ramSize; at += State::RamPage) {
            size_t count = std::min(State::RamPage, s->ramSize - at);
            auto& page = s->ram[at / State::RamPage];
            page.reset(new (std::nothrow) uint8_t[count]);
            if (!page) { error_ = "Not enough memory for cartridge RAM"; return false; }
            memset(page.get(), 0xff, count);
        }
        s->savePath = path + ".sav";
        bool primary = files.exists(s->savePath), backup = files.exists(s->savePath + ".bak");
        if (primary || backup) {
            if (!s->load(s->savePath)) {
                if (!s->load(s->savePath + ".bak")) { error_ = "Unreadable save; preserve it before retrying"; return false; }
                s->recovered = s->dirty = true;
                saveError_ = "Recovered backup save";
            }
        }
    }
    char name[17]{}; gb_get_rom_name(&s->gb, name); title_ = name;
    gb_init_lcd(&s->gb, State::line);
    s->gb.direct.frame_skip = true; s->gb.direct.interlace = false;
    state_ = std::move(s); return true;
}
void GameBoy::close() { state_.reset(); currentApu = nullptr; paused_ = false; }
bool GameBoy::active() const { return bool(state_); }
uint32_t GameBoy::frames() const { return state_ ? state_->frames : 0; }
uint32_t GameBoy::cacheMisses() const { return state_ ? state_->cacheMisses : 0; }
void GameBoy::buttons(uint8_t pressed) { if (state_) state_->gb.direct.joypad = ~pressed; }
void GameBoy::display(DrawLine draw, void* context) { if (state_) { state_->draw = draw; state_->drawContext = context; } }
bool GameBoy::step(unsigned instructions) {
    if (!state_ || paused_ || !error_.empty()) return false;
    auto& s = *state_; currentApu = &s.apu;
    if (!s.inFrame) { s.gb.gb_frame = false; s.inFrame = true; }
    while (instructions-- && !s.gb.gb_frame && error_.empty()) __gb_step_cpu(&s.gb);
    if (!error_.empty()) { paused_ = true; return false; }
    if (s.gb.gb_frame) { s.inFrame = false; ++s.frames; return true; }
    return false;
}
void GameBoy::sound(int16_t* samples) { if (state_) minigb_apu_audio_callback(&state_->apu, samples); }
bool GameBoy::save() {
    if (!state_ || !state_->dirty || !state_->ramSize) return true;
    auto& s = *state_; auto& files = *s.files;
    auto tmp = s.savePath + ".tmp", backup = s.savePath + ".bak";
    auto fail = [&] { saveError_ = "SD save failed; retry before powering off"; return false; };
    auto writer = files.openWriter(tmp);
    if (!writer) return fail();
    uint32_t expected = 2166136261u;
    for (size_t at = 0; at < s.ramSize; at += 512) {
        auto* bytes = s.ram[at / State::RamPage].get() + at % State::RamPage;
        size_t count = std::min<size_t>(512, s.ramSize - at);
        expected = hashBytes(expected, bytes, count);
        if (!writer->write(std::string(reinterpret_cast<char*>(bytes), count))) return fail();
    }
    if (!writer->finish()) return fail();
    writer.reset();
    if (!verifySave(files, tmp, expected, s.ramSize)) return fail();
    if (files.exists(s.savePath)) {
        if (s.recovered) { if (!files.remove(s.savePath)) return fail(); }
        else {
            if (files.exists(backup) && !files.remove(backup)) return fail();
            if (!files.rename(s.savePath, backup)) return fail();
        }
    }
    if (!files.rename(tmp, s.savePath)) return fail();
    s.recovered = s.dirty = false; saveError_.clear(); return true;
}
uint8_t gameBoyButton(const std::string& key) {
    if (key == "z" || key == "a-button" || key == " ") return JOYPAD_A;
    if (key == "x" || key == "b-button" || key == "backspace") return JOYPAD_B;
    if (key == "enter" || key == "start") return JOYPAD_START;
    if (key == "c" || key == "select") return JOYPAD_SELECT;
    if (key == "d" || key == "/" || key == "right") return JOYPAD_RIGHT;
    if (key == "a" || key == "," || key == "left") return JOYPAD_LEFT;
    if (key == "w" || key == ";" || key == "up") return JOYPAD_UP;
    if (key == "s" || key == "." || key == "down") return JOYPAD_DOWN;
    return 0;
}
void gameBoyRenderLine(uint8_t* screen, const uint8_t* pixels, unsigned line) {
    if (!screen || !pixels || line >= 144) return;
    unsigned y = line * 135 / 144;
    if (line && (line - 1) * 135 / 144 == y) return;
    constexpr auto rgb = [](uint8_t r, uint8_t g, uint8_t b) -> uint8_t { return (r & 0xe0) | ((g >> 3) & 0x1c) | (b >> 6); };
    static constexpr uint8_t colors[3][4] = {
        {rgb(255,255,255), rgb(255,150,120), rgb(180,40,40), 0},
        {rgb(255,255,255), rgb(150,220,150), rgb(30,110,60), 0},
        {rgb(255,255,255), rgb(240,190,125), rgb(150,85,55), 0}
    };
    for (unsigned x = 0; x < 150; ++x) {
        uint8_t pixel = pixels[x * 160 / 150];
        screen[y * 240 + x + 45] = colors[std::min<unsigned>((pixel >> 4) & 3, 2)][pixel & 3];
    }
}
}
