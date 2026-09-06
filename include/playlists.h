#pragma once
#include <cstdint>
#include <string>
#include <vector>

namespace ct {
struct Playlist {
    uint32_t id = 0;
    std::string name;
    std::vector<std::string> paths;
};
struct PlaylistSummary {
    uint32_t id;
    std::string name;
    size_t count;
};
class PlaylistFiles {
public:
    virtual ~PlaylistFiles() = default;
    virtual bool ready() const = 0;
    virtual bool exists(const std::string& path) = 0;
    virtual bool read(const std::string& path, std::string& data, size_t maximum) = 0;
    virtual bool write(const std::string& path, const std::string& data) = 0;
    virtual bool rename(const std::string& from, const std::string& to) = 0;
    virtual bool remove(const std::string& path) = 0;
};
class Playlists {
public:
    static constexpr uint32_t MaxLists = 16, MaxTracks = 128, MaxBytes = 32768;
    explicit Playlists(PlaylistFiles& files) : files_(files) {}
    bool begin();
    const std::vector<PlaylistSummary>& list() const { return list_; }
    bool load(uint32_t id, Playlist& playlist);
    bool create(const std::string& name, uint32_t& id);
    bool save(const Playlist& playlist);
    bool erase(uint32_t id);
    const std::string& error() const { return error_; }
    static bool validName(const std::string& name);
    static bool decode(const std::string& data, Playlist& playlist);
    static std::string encode(const Playlist& playlist);
private:
    PlaylistFiles& files_;
    std::vector<PlaylistSummary> list_;
    std::string error_;
    bool fail(const char* error) { error_ = error; return false; }
    static std::string path(uint32_t id);
};
}
