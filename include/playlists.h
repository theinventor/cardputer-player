#pragma once
#include <cstdint>
#include <string>
#include <vector>
#include <functional>
#include <memory>
#include "media.h"

namespace ct {
class PlaylistFiles;
class PlaylistPaths {
public:
    size_t size() const { return offsets_.size(); }
    bool empty() const { return offsets_.empty(); }
    void clear() { offsets_.clear(); added_.clear(); }
    void push_back(const std::string& path);
    bool read(size_t position, std::string& path) const;
    // Reuse one file handle for a batch of random position lookups.
    std::unique_ptr<Reader> openReader() const;
    bool read(Reader* file, size_t position, std::string& path) const;
    bool each(const std::function<bool(size_t, const std::string&)>& visit) const;
    bool contains(const std::string& path) const;
    void erase(size_t position);
    void move(size_t from, size_t to);
private:
    friend class Playlists;
    std::vector<uint32_t> offsets_;
    std::vector<std::string> added_;
    PlaylistFiles* files_ = nullptr;
    std::string file_;
};
struct Playlist {
    uint32_t id = 0;
    std::string name;
    PlaylistPaths paths;
    bool allowRepeats = false;
};
struct PlaylistSummary {
    uint32_t id;
    std::string name;
    size_t count;
};
class PlaylistWriter {
public:
    virtual ~PlaylistWriter() = default;
    virtual bool write(const std::string& chunk) = 0;
    virtual bool finish() = 0;
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
    virtual std::unique_ptr<Reader> openReader(const std::string& path) { return nullptr; }
    virtual std::unique_ptr<PlaylistWriter> openWriter(const std::string& path) { return nullptr; }
};
class Playlists {
public:
    static constexpr uint32_t MaxLists = 16, MaxTracks = 1000, MaxBytes = 400000;
    explicit Playlists(PlaylistFiles& files) : files_(files) {}
    bool begin();
    const std::vector<PlaylistSummary>& list() const { return list_; }
    bool load(uint32_t id, Playlist& playlist);
    bool create(const std::string& name, uint32_t& id);
    bool save(const Playlist& playlist);
    bool importFile(const std::string& source, uint32_t& id);
    bool erase(uint32_t id);
    const std::string& error() const { return error_; }
    static bool validName(const std::string& name);
    static bool decode(const std::string& data, Playlist& playlist);
    static std::string encode(const Playlist& playlist);
    static uint64_t pathHash(const std::string& path);
private:
    PlaylistFiles& files_;
    std::vector<PlaylistSummary> list_;
    std::string error_;
    bool fail(const char* error) { error_ = error; return false; }
    static std::string path(uint32_t id);
    bool readFile(const std::string& file, Playlist& playlist);
    bool readLegacy(const std::string& file, Playlist& playlist);
};
}
