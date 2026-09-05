#include "storage.h"
#include <SPI.h>

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
    mounted_ = SD.begin(12, SPI, 20000000);
    if (!mounted_) return false;
    SD.mkdir("/.cardtunes");
    SD.mkdir("/Music");
    if (!SD.exists(Index) && SD.exists(Backup)) SD.rename(Backup, Index);
    File index = SD.open(Index, FILE_READ);
    if (index && index.size() % sizeof(Track) == 0 && index.size() / sizeof(Track) <= MaxTracks) {
        count_ = index.size() / sizeof(Track); return true;
    }
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
    ++count_; return true;
}
bool Library::walk(const std::string& path, unsigned depth, File& index, void (*progress)(uint32_t)) {
    File dir = SD.open(path.c_str(), FILE_READ);
    if (!dir || !dir.isDirectory()) return false;
    while (File entry = dir.openNextFile()) {
        std::string name = basename(entry.name());
        std::string child = path == "/" ? path + name : path + "/" + name;
        bool directory = entry.isDirectory(); entry.close();
        if (name.empty() || name[0] == '.') continue;
        if (directory) {
            if (depth < 8 && child.size() < 192) { if (!walk(child, depth + 1, index, progress)) return false; }
            else ++skipped_;
        } else if (isMp3(child)) {
            if (count_ >= MaxTracks || !validMusicPath(child)) ++skipped_;
            else if (!record(child.c_str(), index)) return false;
            if (progress) progress(count_);
        }
        delay(1);
    }
    return true;
}
bool Library::scan(void (*progress)(uint32_t)) {
    if (!mounted_) return false;
    SDLock lock(sdMutex);
    uint32_t oldCount = count_;
    count_ = skipped_ = 0;
    File index = SD.open(Temp, FILE_WRITE);
    if (!index) { count_ = oldCount; return false; }
    bool ok = walk("/", 0, index, progress);
    index.flush(); index.close();
    if (!ok) { SD.remove(Temp); count_ = oldCount; return false; }
    SD.remove(Backup);
    if (SD.exists(Index) && !SD.rename(Index, Backup)) { count_ = oldCount; return false; }
    if (!SD.rename(Temp, Index)) { SD.rename(Backup, Index); count_ = oldCount; return false; }
    SD.remove(Backup);
    return true;
}
bool Library::append(const char* path) {
    if (!mounted_ || count_ >= MaxTracks || !validMusicPath(path)) return false;
    SDLock lock(sdMutex);
    File file = SD.open(Index, FILE_APPEND);
    if (!file) return false;
    bool ok = record(path, file);
    file.flush(); file.close(); return ok;
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
    Track track;
    for (uint32_t i = 0; i < count(); ++i) if (get(i, track) && !strcmp(track.path, path)) return i;
    return -1;
}
}
