#pragma once
#include "playlists.h"
#include <esp_partition.h>

namespace ct {
class FlashRom {
public:
    ~FlashRom() { reset(); }
    bool prepare(PlaylistFiles& files, const std::string& path);
    void reset();
    const uint8_t* data() const { return data_; }
    const std::string& error() const { return error_; }
private:
    const uint8_t* data_ = nullptr;
    esp_partition_mmap_handle_t handle_ = 0;
    std::string error_;
    bool fail(const char* message) { reset(); error_ = message; return false; }
};
}
