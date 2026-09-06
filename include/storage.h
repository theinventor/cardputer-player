#pragma once
#include <SD.h>
#include <LittleFS.h>
#include <mutex>
#include "media.h"
#include "playlists.h"

namespace ct {
extern std::recursive_mutex sdMutex;
using SDLock = std::lock_guard<std::recursive_mutex>;
class SDReader : public Reader {
public:
    bool open(const char* path) { SDLock lock(sdMutex); file_ = !strcmp(path, "/@demo.mp3") ? LittleFS.open("/demo.mp3", FILE_READ) : SD.open(path, FILE_READ); size_ = file_ ? file_.size() : 0; return bool(file_); }
    void close() { SDLock lock(sdMutex); file_.close(); size_ = 0; }
    size_t read(void* data, size_t bytes) override { SDLock lock(sdMutex); return file_.read(static_cast<uint8_t*>(data), bytes); }
    bool seek(uint32_t offset) override { SDLock lock(sdMutex); return offset <= size_ && file_.seek(offset); }
    uint32_t size() const override { return size_; }
private:
    File file_;
    uint32_t size_ = 0;
};
struct Track {
    char path[192]{};
    char title[64]{};
    char artist[48]{};
    char album[48]{};
};
template<size_t N> void copyText(char (&target)[N], const std::string& source) {
    size_t length = std::min(source.size(), N - 1);
    while (length && length < source.size() && (uint8_t(source[length]) & 0xc0) == 0x80) --length;
    memcpy(target, source.data(), length); target[length] = 0;
}
class Library {
public:
    static constexpr uint32_t MaxTracks = 10000;
    bool begin();
    bool scan(void (*progress)(uint32_t) = nullptr);
    bool append(const char* path);
    bool get(uint32_t id, Track& track);
    bool available(const char* path);
    int find(const std::string& query, int start, int direction = 1);
    int byPath(const char* path);
    uint32_t count() const { return count_ + (demo_ ? 1 : 0); }
    uint32_t skipped() const { return skipped_; }
    bool mounted() const { return mounted_; }
private:
    bool walk(const std::string& path, unsigned depth, File& index, void (*progress)(uint32_t));
    bool record(const char* path, File& index);
    bool mounted_ = false;
    bool demo_ = false;
    uint32_t count_ = 0, skipped_ = 0;
};
class SDPlaylistFiles : public PlaylistFiles {
public:
    explicit SDPlaylistFiles(Library& library) : library_(library) {}
    bool ready() const override { return library_.mounted(); }
    bool exists(const std::string& path) override { SDLock lock(sdMutex); return SD.exists(path.c_str()); }
    bool read(const std::string& path, std::string& data, size_t maximum) override;
    bool write(const std::string& path, const std::string& data) override;
    bool rename(const std::string& from, const std::string& to) override { SDLock lock(sdMutex); return SD.rename(from.c_str(), to.c_str()); }
    bool remove(const std::string& path) override { SDLock lock(sdMutex); return SD.remove(path.c_str()); }
private:
    Library& library_;
};
}
