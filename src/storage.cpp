#include "storage.h"
#include <SPI.h>
#include <cerrno>

namespace ct {
std::recursive_mutex sdMutex;
namespace {
constexpr const char* Index = "/.cardtunes/library-v1.bin";
constexpr const char* Temp = "/.cardtunes/library.tmp";
constexpr const char* Backup = "/.cardtunes/library.bak";
}
bool Library::begin() {
    SDLock lock(sdMutex);
    demo_ = LittleFS.begin(false) && LittleFS.exists("/demo.mp3");
    SPI.begin(40, 39, 14, 12);
    mounted_ = SD.begin(12, SPI, 20000000, mountPoint_);
    if (!mounted_) return false;
    SD.mkdir("/.cardtunes");
    SD.mkdir("/Music");
    if (!SD.exists(Index) && SD.exists(Backup)) SD.rename(Backup, Index);
    File index = SD.open(Index, FILE_READ);
    if (index && index.size() % sizeof(Track) == 0 && index.size() / sizeof(Track) <= MaxTracks) {
        count_ = index.size() / sizeof(Track); return true;
    }
    index.close();
    return scan();
}
bool Library::record(const char* path, File& index) {
    SDReader reader;
    if (!reader.open(path)) return false;
    auto tags = readTags(reader);
    reader.close();
    Track track;
    copyText(track.path, path);
    std::string name = basename(path);
    if (tags.title.empty()) tags.title = name.substr(0, name.size() - 4);
    copyText(track.title, tags.title);
    copyText(track.artist, tags.artist);
    copyText(track.album, tags.album);
    if (index.write(reinterpret_cast<const uint8_t*>(&track), sizeof(track)) != sizeof(track)) return false;
    return true;
}
bool Library::scan() {
    if (scanning()) return false;
    scanError_.clear(); scanPath_.clear(); scanSucceeded_ = false;
    scanned_ = skipped_ = scanElapsed_ = 0;
    if (!mounted_) { scanError_ = "No microSD detected"; return false; }
    SDLock lock(sdMutex);
    scanStarted_ = millis();
    scanIndex_ = SD.open(Temp, FILE_WRITE);
    if (!scanIndex_) { scanError_ = "Cannot create music index"; return false; }
    directories_[0] = {opendir(mountPoint_), "/"};
    if (!directories_[0].handle) { finishScan(false, "Cannot read microSD directory"); return false; }
    scanDepth_ = 0;
    return true;
}
void Library::scanStep() {
    if (!scanning()) return;
    SDLock lock(sdMutex);
    auto& directory = directories_[scanDepth_];
    errno = 0;
    auto* entry = readdir(directory.handle);
    if (!entry) {
        if (errno) { finishScan(false, "Cannot read microSD directory"); return; }
        closedir(directory.handle); directory.handle = nullptr; directory.path.clear();
        if (!scanDepth_) finishScan(true);
        else --scanDepth_;
        return;
    }
    // Enumerate names without opening every file twice, including macOS sidecars.
    if (!entry->d_name[0] || entry->d_name[0] == '.') return;
    scanPath_ = directory.path == "/" ? directory.path + entry->d_name : directory.path + "/" + entry->d_name;
    if (entry->d_type == DT_DIR) {
        if (scanDepth_ == int(directories_.size()) - 1 || scanPath_.size() >= sizeof(Track::path)) { ++skipped_; return; }
        auto* handle = opendir((std::string(mountPoint_) + scanPath_).c_str());
        if (!handle) { finishScan(false, "Cannot open music folder"); return; }
        directories_[++scanDepth_] = {handle, scanPath_};
    } else if (entry->d_type == DT_REG && isMp3(scanPath_)) {
        if (scanned_ >= MaxTracks || !validMusicPath(scanPath_)) { ++skipped_; return; }
        if (!record(scanPath_.c_str(), scanIndex_)) { finishScan(false, "Cannot read song or write music index"); return; }
        ++scanned_;
    }
}
void Library::finishScan(bool success, const char* error) {
    SDLock lock(sdMutex);
    for (auto& directory : directories_) {
        if (directory.handle) closedir(directory.handle);
        directory.handle = nullptr; directory.path.clear();
    }
    scanDepth_ = -1; scanElapsed_ = millis() - scanStarted_;
    scanError_ = error;
    scanIndex_.flush();
    if (success && scanIndex_.size() != scanned_ * sizeof(Track)) { success = false; scanError_ = "Incomplete music index"; }
    scanIndex_.close();
    if (success) {
        if ((SD.exists(Backup) && !SD.remove(Backup)) || (SD.exists(Index) && !SD.rename(Index, Backup))) {
            success = false; scanError_ = "Cannot back up music index";
        } else if (!SD.rename(Temp, Index)) {
            if (SD.exists(Backup)) SD.rename(Backup, Index);
            success = false; scanError_ = "Cannot save music index";
        }
    }
    if (success) { count_ = scanned_; SD.remove(Backup); }
    else SD.remove(Temp);
    scanSucceeded_ = success;
}
void Library::cancelScan() { if (scanning()) finishScan(false, "Scan cancelled"); }
bool Library::append(const char* path) {
    if (scanning() || !mounted_ || count_ >= MaxTracks || !validMusicPath(path)) return false;
    SDLock lock(sdMutex);
    File file = SD.open(Index, FILE_APPEND);
    if (!file) return false;
    bool ok = record(path, file);
    file.flush(); file.close(); if (ok) ++count_; return ok;
}
bool Library::get(uint32_t id, Track& track) {
    if (demo_ && id == 0) {
        track = Track{};
        copyText(track.path, "/@demo.mp3");
        SDReader reader;
        if (!reader.open(track.path)) return false;
        auto tags = readTags(reader); reader.close();
        copyText(track.title, tags.title.empty() ? "Demo track" : tags.title);
        copyText(track.artist, tags.artist);
        copyText(track.album, tags.album);
        return true;
    }
    if (demo_) --id;
    if (id >= count_) return false;
    SDLock lock(sdMutex);
    File index = SD.open(Index, FILE_READ);
    bool ok = index && index.seek(id * sizeof(Track)) && index.read(reinterpret_cast<uint8_t*>(&track), sizeof(track)) == sizeof(track);
    if (!ok) return false;
    track.path[sizeof(track.path) - 1] = 0;
    track.title[sizeof(track.title) - 1] = 0;
    track.artist[sizeof(track.artist) - 1] = 0;
    track.album[sizeof(track.album) - 1] = 0;
    return validMusicPath(track.path);
}
int Library::find(const std::string& query, int start, int direction) {
    Track track;
    for (int i = start; i >= 0 && i < int(count()); i += direction) {
        if (get(i, track) && (query.empty() || contains(track.title, query) || contains(track.artist, query) || contains(track.album, query) || contains(track.path, query))) return i;
        if (!(i % 32)) delay(1);
    }
    return -1;
}
int Library::byPath(const char* path) {
    int found = -1;
    if (!path || !*path) return found;
    each([&](uint32_t id, const Track& track) { if (!strcmp(track.path, path)) { found = id; return false; } return true; });
    return found;
}
bool Library::each(const std::function<bool(uint32_t, const Track&)>& visit) {
    Track track;
    if (demo_) { if (!get(0, track) || !visit(0, track)) return false; }
    if (!count_) return true;
    File index;
    { SDLock lock(sdMutex); index = SD.open(Index, FILE_READ); }
    if (!index) return false;
    for (uint32_t i = 0; i < count_; ++i) {
        { SDLock lock(sdMutex); if (index.read(reinterpret_cast<uint8_t*>(&track), sizeof(track)) != sizeof(track)) return false; }
        track.path[sizeof(track.path) - 1] = 0; track.title[sizeof(track.title) - 1] = 0;
        track.artist[sizeof(track.artist) - 1] = 0; track.album[sizeof(track.album) - 1] = 0;
        if (!validMusicPath(track.path) || !visit(i + (demo_ ? 1 : 0), track)) return false;
        if (!(i % 32)) delay(1);
    }
    return true;
}
bool Library::available(const char* path) {
    SDLock lock(sdMutex);
    File file = !strcmp(path, "/@demo.mp3") ? LittleFS.open("/demo.mp3", FILE_READ) : SD.open(path, FILE_READ);
    return file && !file.isDirectory() && file.size() > 0;
}
namespace {
class PlaylistSDReader : public SDReader {
public:
    size_t read(void* data, size_t bytes) override {
        size_t result = SDReader::read(data, bytes);
        if (!(++reads_ % 16)) delay(1);
        return result;
    }
private:
    unsigned reads_ = 0;
};
class PlaylistSDWriter : public PlaylistWriter {
public:
    explicit PlaylistSDWriter(File file) : file_(file) {}
    bool write(const std::string& chunk) override {
        bool ok;
        { SDLock lock(sdMutex); ok = file_.write(reinterpret_cast<const uint8_t*>(chunk.data()), chunk.size()) == chunk.size(); }
        if (!(++writes_ % 16)) delay(1);
        return ok;
    }
    bool finish() override { SDLock lock(sdMutex); file_.flush(); file_.close(); return true; }
private:
    File file_;
    unsigned writes_ = 0;
};
}
std::unique_ptr<Reader> SDPlaylistFiles::openReader(const std::string& path) {
    auto file = std::make_unique<PlaylistSDReader>();
    if (!file->open(path.c_str())) return nullptr;
    return file;
}
std::unique_ptr<PlaylistWriter> SDPlaylistFiles::openWriter(const std::string& path) {
    SDLock lock(sdMutex);
    File file = SD.open(path.c_str(), FILE_WRITE);
    if (!file) return nullptr;
    return std::make_unique<PlaylistSDWriter>(file);
}
bool SDPlaylistFiles::read(const std::string& path, std::string& data, size_t maximum) {
    SDLock lock(sdMutex);
    File file = SD.open(path.c_str(), FILE_READ);
    if (!file || file.isDirectory() || file.size() > maximum) return false;
    data.resize(file.size());
    if (data.empty()) return true;
    return file.read(reinterpret_cast<uint8_t*>(&data[0]), data.size()) == data.size();
}
bool SDPlaylistFiles::write(const std::string& path, const std::string& data) {
    SDLock lock(sdMutex);
    File file = SD.open(path.c_str(), FILE_WRITE);
    if (!file) return false;
    bool ok = file.write(reinterpret_cast<const uint8_t*>(data.data()), data.size()) == data.size();
    file.flush(); file.close(); return ok;
}
}
