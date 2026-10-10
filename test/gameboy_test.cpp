#include "gameboy.h"
#include "rom_upload.h"
#include <cassert>
#include <cstring>
#include <fstream>
#include <iostream>
#include <map>
#include <vector>

class Files : public ct::PlaylistFiles {
public:
    std::map<std::string, std::string> data;
    bool failWrite = false, failRename = false, corrupt = false, failRead = false;
    class Input : public ct::Reader {
    public:
        Input(Files& f, std::string p) : f(f), p(std::move(p)) {}
        size_t read(void* out, size_t count) override {
            if (f.failRead) return 0;
            auto& value = f.data.at(p); count = std::min(count, value.size() - at);
            memcpy(out, value.data() + at, count); at += count; return count;
        }
        bool seek(uint32_t offset) override { if (offset > size()) return false; at = offset; return true; }
        uint32_t size() const override { return f.data.at(p).size(); }
        Files& f; std::string p; size_t at = 0;
    };
    class Output : public ct::PlaylistWriter {
    public:
        Output(Files& f, std::string p) : f(f), p(std::move(p)) { f.data[this->p].clear(); }
        bool write(const std::string& value) override {
            if (f.failWrite) return false;
            f.data[p] += value;
            if (f.corrupt && !value.empty()) f.data[p].back() ^= 1;
            return true;
        }
        bool finish() override { return true; }
        Files& f; std::string p;
    };
    bool ready() const override { return true; }
    bool exists(const std::string& p) override { return data.count(p); }
    bool read(const std::string&, std::string&, size_t) override { assert(false); return false; }
    bool write(const std::string&, const std::string&) override { assert(false); return false; }
    bool rename(const std::string& from, const std::string& to) override {
        if ((failRename && from.find(".tmp") != std::string::npos) || !exists(from) || exists(to)) return false;
        data[to] = std::move(data[from]); data.erase(from); return true;
    }
    bool remove(const std::string& p) override { return data.erase(p); }
    std::unique_ptr<ct::Reader> openReader(const std::string& p) override {
        return exists(p) ? std::make_unique<Input>(*this, p) : nullptr;
    }
    std::unique_ptr<ct::PlaylistWriter> openWriter(const std::string& p) override { return std::make_unique<Output>(*this, p); }
};
static void checksum(std::string& rom) {
    uint8_t sum = 0; for (unsigned i = 0x134; i <= 0x14c; ++i) sum = sum - uint8_t(rom[i]) - 1;
    rom[0x14d] = sum;
}
static std::string fixture() {
    std::string rom(65536, '\0');
    rom[0x100] = char(0xc3); rom[0x101] = 0x50; rom[0x102] = 0x01;
    memcpy(rom.data() + 0x134, "CARDTUNES TEST", 14);
    rom[0x147] = 0x13; rom[0x148] = 1; rom[0x149] = 3;
    // Enable SRAM, write one byte, then exercise banked ROM fetches in a loop.
    std::vector<uint8_t> code = {0x3e,0x0a,0xea,0x00,0x00};
    for (uint8_t bank = 0; bank < 4; ++bank) {
        const uint8_t writes[] = {0x3e,bank,0xea,0x00,0x40,0x3e,uint8_t(0x42 + bank),0xea,0x00,0xa0,0xea,0xff,0xbf};
        code.insert(code.end(), std::begin(writes), std::end(writes));
    }
    code.insert(code.end(), {0xc3,0x00,0x40});
    memcpy(rom.data() + 0x150, code.data(), code.size());
    rom[0x4000] = char(0xc3); rom[0x4001] = 0; rom[0x4002] = 0x40;
    checksum(rom); return rom;
}
static void run(ct::GameBoy& gb, unsigned frames) {
    unsigned limit = frames * 100;
    auto target = gb.frames() + frames;
    while (gb.frames() < target && limit-- && gb.error().empty()) gb.step();
    assert(gb.error().empty()); assert(gb.frames() == target);
}
int main(int argc, char** argv) {
    {
        Files disk; ct::RomUpload upload(disk); auto rom = fixture();
        auto hash = ct::RomUpload::hash(2166136261u, reinterpret_cast<const uint8_t*>(rom.data()), rom.size());
        assert(!upload.begin("/Music/red.gb", rom.size(), hash));
        assert(upload.begin("/Games/usb.gb", rom.size(), hash));
        assert(!upload.finish()); assert(!upload.chunk(1, "00"));
        assert(!upload.chunk(0, "zz") && upload.received() == 0);
        auto transfer = [&] {
            for (size_t at = 0; at < rom.size(); at += 512) {
                std::string hex; const char* digits = "0123456789abcdef";
                for (size_t i = at; i < at + 512; ++i) { uint8_t c = rom[i]; hex += digits[c >> 4]; hex += digits[c & 15]; }
                assert(upload.chunk(at, hex));
            }
        };
        transfer();
        assert(upload.finish() && disk.data["/Games/usb.gb"] == rom);
        assert(!upload.begin("/Games/usb.gb", rom.size(), hash));
        assert(upload.begin("/Games/bad.gb", rom.size(), hash));
        assert(upload.chunk(0, "00")); upload.abort(); assert(!upload.active());
        assert(!disk.exists("/Games/bad.gb") && !disk.exists("/Games/.usb-upload.tmp"));
        assert(upload.begin("/Games/bad.gb", rom.size(), hash ^ 1)); transfer();
        assert(!upload.finish() && !disk.exists("/Games/bad.gb"));
        assert(upload.begin("/Games/bad.gb", rom.size(), hash));
        disk.corrupt = true; transfer(); disk.corrupt = false;
        assert(!upload.finish() && !disk.exists("/Games/bad.gb"));
        assert(upload.begin("/Games/bad.gb", rom.size(), hash));
        disk.failWrite = true; assert(!upload.chunk(0, "00") && !upload.active()); disk.failWrite = false;
        assert(upload.begin("/Games/bad.gb", rom.size(), hash)); transfer();
        disk.failRename = true; assert(!upload.finish() && !disk.exists("/Games/bad.gb")); disk.failRename = false;
        assert(disk.data["/Games/usb.gb"] == rom);
    }
    std::vector<uint8_t> screen(240 * 135, 0x55);
    uint8_t row[160]{};
    for (unsigned line = 0; line <= 144; ++line) ct::gameBoyRenderLine(screen.data(), row, line);
    for (unsigned y = 0; y < 135; ++y) for (unsigned x = 0; x < 240; ++x)
        assert(screen[y * 240 + x] == (x >= 45 && x < 195 ? 255 : 0x55));
    const std::string path = ct::GameBoy::DefaultRom, save = path + ".sav";
    Files files; files.data[path] = fixture();
    ct::GameBoy gb;
    assert(!ct::GameBoy::validPath("/Music/red.gb"));
    assert(!ct::GameBoy::validPath("/Games/../red.gb"));
    assert(!ct::GameBoy::validPath("/Games/red.gba"));
    assert(ct::gameBoyButton("enter") == 8 && ct::gameBoyButton("z") == 1);
    assert(gb.open(files, path)); run(gb, 4); assert(gb.save());
    assert(files.data[save].size() == 32768 && files.data[save][0] == 0x42);
    for (unsigned bank = 0; bank < 4; ++bank) {
        assert(files.data[save][bank * 8192] == 0x42 + bank);
        assert(files.data[save][bank * 8192 + 8191] == 0x42 + bank);
    }
    gb.pause(true); auto before = gb.frames(); assert(!gb.step() && gb.frames() == before);
    gb.close();
    files.data[save][0] = 1;
    assert(gb.open(files, path)); run(gb, 2);
    files.failWrite = true; assert(!gb.save() && files.data[save][0] == 1);
    files.failWrite = false; files.corrupt = true; assert(!gb.save() && files.data[save][0] == 1);
    files.corrupt = false; files.failRename = true; assert(!gb.save());
    assert(files.data[save + ".bak"][0] == 1); gb.close();
    files.failRename = false;
    assert(gb.open(files, path)); assert(!gb.saveError().empty()); run(gb, 2); assert(gb.save()); gb.close();
    assert(files.data[save][0] == 0x42);
    files.data[save] = "broken"; files.data[save + ".bak"] = "broken";
    assert(!gb.open(files, path)); assert(files.data[save] == "broken");
    files.data.erase(save); files.data.erase(save + ".bak");
    for (unsigned offset : {0x148u, 0x149u, 0x147u}) {
        files.data[path] = fixture(); files.data[path][offset] = char(0xff); checksum(files.data[path]);
        assert(!gb.open(files, path));
    }
    files.data[path] = fixture(); files.data[path][0x143] = char(0xc0); checksum(files.data[path]); assert(!gb.open(files, path));
    files.data[path] = fixture(); files.data[path].resize(200); assert(!gb.open(files, path));
    files.data[path] = fixture(); assert(gb.open(files, path)); files.failRead = true;
    for (unsigned i = 0; i < 100 && gb.error().empty(); ++i) gb.step();
    assert(!gb.error().empty()); gb.close(); files.failRead = false;
    assert(gb.open(files, path, reinterpret_cast<const uint8_t*>(files.data[path].data())));
    files.failRead = true; run(gb, 10); assert(gb.cacheMisses() == 0);
    files.failRead = false; assert(gb.save()); gb.close();
    if (argc > 1) {
        std::ifstream input(argv[1], std::ios::binary); assert(input);
        files.data[path] = std::string(std::istreambuf_iterator<char>(input), {});
        assert(gb.open(files, path));
        struct Pixels { size_t lines = 0, dark = 0; uint8_t screen[240 * 135]{}; } pixels;
        gb.display([](void* context, const uint8_t* row, unsigned line) {
            auto& p = *static_cast<Pixels*>(context); ++p.lines;
            for (unsigned x = 0; x < 160; ++x) if ((row[x] & 3) != 0) ++p.dark;
            ct::gameBoyRenderLine(p.screen, row, line);
        }, &pixels);
        int16_t pcm[ct::GameBoy::Samples]; bool audible = false;
        for (unsigned i = 0; i < 1800; ++i) {
            gb.buttons(i % 120 < 5 ? ct::gameBoyButton("start") : 0);
            run(gb, 1); gb.sound(pcm);
            for (auto sample : pcm) audible |= sample != 0;
        }
        assert(pixels.lines && pixels.dark && audible);
        std::ofstream image("build/gameboy-preview.ppm", std::ios::binary);
        image << "P6\n240 135\n255\n";
        for (uint8_t color : pixels.screen) {
            image.put((color >> 5) * 255 / 7);
            image.put(((color >> 2) & 7) * 255 / 7);
            image.put((color & 3) * 255 / 3);
        }
        std::cout << "Local ROM: " << gb.title() << ", " << gb.frames() << " frames, nonblank video and audio\n";
    }
    std::cout << "Game Boy validation, paging, input, pause, save recovery and write failures passed\n";
}
