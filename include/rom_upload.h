#pragma once
#include "gameboy.h"

namespace ct {
class RomUpload {
public:
    explicit RomUpload(PlaylistFiles& files) : files_(files) {}
    bool begin(const std::string& path, uint32_t size, uint32_t hash);
    bool chunk(uint32_t offset, const std::string& hex);
    bool finish();
    void abort();
    bool active() const { return bool(writer_); }
    uint32_t received() const { return received_; }
    const std::string& error() const { return error_; }
    static uint32_t hash(uint32_t value, const uint8_t* data, size_t count);
private:
    static constexpr const char* Temp = "/Games/.usb-upload.tmp";
    PlaylistFiles& files_;
    std::unique_ptr<PlaylistWriter> writer_;
    std::string path_, error_;
    uint32_t size_ = 0, received_ = 0, hash_ = 0;
    bool fail(const char* message) { error_ = message; return false; }
};
}
