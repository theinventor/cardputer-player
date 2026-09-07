#include "playlists.h"
#include <cJSON.h>
#include <algorithm>
#include <cctype>
#include <cstring>

namespace ct {
namespace {
constexpr uint32_t Added = 0x80000000;
constexpr size_t LineBytes = 512;
bool line(Reader& file, uint32_t& offset, std::string& value) {
    char buffer[LineBytes];
    if (!file.seek(offset)) return false;
    size_t n = file.read(buffer, sizeof(buffer));
    auto end = static_cast<const char*>(memchr(buffer, '\n', n));
    if (!end) return false;
    value.assign(buffer, end - buffer);
    offset += value.size() + 1;
    return value.find('\0') == std::string::npos;
}
std::string jsonText(cJSON* json) {
    if (!json) return {};
    char buffer[LineBytes];
    bool ok = cJSON_PrintPreallocated(json, buffer, sizeof(buffer), false);
    cJSON_Delete(json);
    return ok ? std::string(buffer) + "\n" : std::string();
}
std::string header(const Playlist& playlist) {
    auto json = cJSON_CreateObject();
    if (!json || !cJSON_AddNumberToObject(json, "version", 2) ||
        !cJSON_AddStringToObject(json, "name", playlist.name.c_str()) ||
        !cJSON_AddNumberToObject(json, "count", playlist.paths.size())) { cJSON_Delete(json); return {}; }
    return jsonText(json);
}
bool parsePath(const std::string& data, std::string& path) {
    cJSON* json = cJSON_ParseWithOpts(data.c_str(), nullptr, true);
    bool ok = cJSON_IsString(json) && validMusicPath(json->valuestring);
    if (ok) path = json->valuestring;
    cJSON_Delete(json);
    return ok;
}
}
void PlaylistPaths::push_back(const std::string& path) {
    offsets_.push_back(Added | added_.size()); added_.push_back(path);
}
bool PlaylistPaths::read(Reader* file, size_t position, std::string& path) const {
    if (position >= size()) return false;
    uint32_t offset = offsets_[position];
    if (offset & Added) { path = added_.at(offset & ~Added); return true; }
    std::string data;
    return file && line(*file, offset, data) && parsePath(data, path);
}
bool PlaylistPaths::read(size_t position, std::string& path) const {
    auto file = files_ ? files_->openReader(file_) : nullptr;
    return read(file.get(), position, path);
}
bool PlaylistPaths::each(const std::function<bool(size_t, const std::string&)>& visit) const {
    auto file = files_ ? files_->openReader(file_) : nullptr;
    std::string path;
    for (size_t i = 0; i < size(); ++i) if (!read(file.get(), i, path) || !visit(i, path)) return false;
    return true;
}
bool PlaylistPaths::contains(const std::string& path) const {
    bool found = false;
    each([&](size_t, const std::string& candidate) { found = candidate == path; return !found; });
    return found;
}
void PlaylistPaths::erase(size_t position) { offsets_.erase(offsets_.begin() + position); }
void PlaylistPaths::move(size_t from, size_t to) {
    uint32_t ref = offsets_.at(from); erase(from); offsets_.insert(offsets_.begin() + to, ref);
}
uint64_t Playlists::pathHash(const std::string& path) {
    uint64_t hash = 14695981039346656037ULL;
    for (unsigned char c : path) { hash ^= c; hash *= 1099511628211ULL; }
    return hash;
}
bool Playlists::validName(const std::string& name) {
    if (name.empty() || name.size() > 63 || std::isspace(uint8_t(name.front())) || std::isspace(uint8_t(name.back()))) return false;
    return std::none_of(name.begin(), name.end(), [](unsigned char c) { return c < 32 || c == 127; });
}
// Version 1 is read-only compatibility; new saves stream bounded JSON Lines records.
bool Playlists::decode(const std::string& data, Playlist& playlist) {
    if (data.size() > 32768 || data.find('\0') != std::string::npos) return false;
    cJSON* json = cJSON_ParseWithOpts(data.c_str(), nullptr, true);
    if (!json) return false;
    auto version = cJSON_GetObjectItemCaseSensitive(json, "version");
    auto name = cJSON_GetObjectItemCaseSensitive(json, "name");
    auto paths = cJSON_GetObjectItemCaseSensitive(json, "paths");
    bool ok = cJSON_IsObject(json) && cJSON_IsNumber(version) && version->valuedouble == 1 &&
        cJSON_IsString(name) && validName(name->valuestring) && cJSON_IsArray(paths) && cJSON_GetArraySize(paths) <= 128;
    Playlist result;
    if (ok) {
        result.name = name->valuestring;
        cJSON* item;
        cJSON_ArrayForEach(item, paths) {
            if (!cJSON_IsString(item) || !validMusicPath(item->valuestring) || result.paths.contains(item->valuestring)) { ok = false; break; }
            result.paths.push_back(item->valuestring);
        }
    }
    cJSON_Delete(json);
    if (ok) { result.id = playlist.id; playlist = std::move(result); }
    return ok;
}
std::string Playlists::encode(const Playlist& playlist) {
    if (!validName(playlist.name) || playlist.paths.size() > 128) return {};
    auto json = cJSON_CreateObject();
    if (!json) return {};
    bool ok = cJSON_AddNumberToObject(json, "version", 1) && cJSON_AddStringToObject(json, "name", playlist.name.c_str());
    auto paths = cJSON_AddArrayToObject(json, "paths");
    ok = ok && paths;
    ok = ok && playlist.paths.each([&](size_t, const std::string& path) {
        if (!validMusicPath(path)) return false;
        auto item = cJSON_CreateString(path.c_str());
        if (!item) return false;
        if (!cJSON_AddItemToArray(paths, item)) { cJSON_Delete(item); return false; }
        return true;
    });
    char* text = ok ? cJSON_PrintUnformatted(json) : nullptr;
    std::string data = text ? text : "";
    cJSON_free(text); cJSON_Delete(json);
    return data.size() <= 32768 ? data : std::string();
}
std::string Playlists::path(uint32_t id) { return "/.cardtunes/playlist-" + std::to_string(id) + ".jsonl"; }
bool Playlists::readFile(const std::string& source, Playlist& playlist) {
    auto file = files_.openReader(source);
    if (!file || file->size() > MaxBytes) return false;
    uint32_t offset = 0;
    std::string data;
    if (!line(*file, offset, data)) return false;
    cJSON* json = cJSON_ParseWithOpts(data.c_str(), nullptr, true);
    auto version = cJSON_GetObjectItemCaseSensitive(json, "version");
    auto name = cJSON_GetObjectItemCaseSensitive(json, "name");
    auto count = cJSON_GetObjectItemCaseSensitive(json, "count");
    bool ok = cJSON_IsObject(json) && cJSON_IsNumber(version) && version->valuedouble == 2 &&
        cJSON_IsString(name) && validName(name->valuestring) && cJSON_IsNumber(count) &&
        count->valuedouble >= 0 && count->valuedouble <= MaxTracks && count->valuedouble == count->valueint;
    Playlist result; result.id = playlist.id;
    uint32_t expected = ok ? count->valueint : 0;
    if (ok) result.name = name->valuestring;
    cJSON_Delete(json);
    if (!ok) return false;
    result.paths.files_ = &files_; result.paths.file_ = source;
    result.paths.offsets_.reserve(expected);
    std::vector<uint64_t> hashes; hashes.reserve(expected);
    for (uint32_t i = 0; i < expected; ++i) {
        uint32_t start = offset;
        std::string path;
        if (!line(*file, offset, data) || !parsePath(data, path)) return false;
        uint64_t hash = pathHash(path);
        auto at = std::lower_bound(hashes.begin(), hashes.end(), hash);
        if (at != hashes.end() && *at == hash && result.paths.contains(path)) return false;
        hashes.insert(at, hash); result.paths.offsets_.push_back(start);
    }
    if (offset != file->size()) return false;
    playlist = std::move(result); return true;
}
bool Playlists::readLegacy(const std::string& file, Playlist& playlist) {
    std::string data;
    return files_.read(file, data, 32768) && decode(data, playlist);
}
bool Playlists::begin() {
    list_.clear(); error_.clear();
    if (!files_.ready()) return fail("Insert a microSD card for playlists");
    bool ok = true;
    for (uint32_t id = 1; id <= MaxLists; ++id) {
        auto file = path(id), legacy = file.substr(0, file.size() - 1);
        if (!files_.exists(file) && !files_.exists(file + ".bak") && !files_.exists(legacy) && !files_.exists(legacy + ".bak")) continue;
        Playlist playlist;
        if (load(id, playlist)) list_.push_back({id, playlist.name, playlist.paths.size()}); else ok = false;
    }
    if (!ok) return fail("A playlist could not be read; its file was preserved");
    return true;
}
bool Playlists::load(uint32_t id, Playlist& playlist) {
    if (!files_.ready()) return fail("Insert a microSD card for playlists");
    if (!id || id > MaxLists) return fail("Invalid playlist ID");
    auto file = path(id);
    playlist.id = id;
    bool legacy = !files_.exists(file) && !files_.exists(file + ".bak");
    if (legacy) file.pop_back();
    if (legacy ? readLegacy(file, playlist) : readFile(file, playlist)) return true;
    if (legacy ? readLegacy(file + ".bak", playlist) : readFile(file + ".bak", playlist)) {
        if ((files_.exists(file) && !files_.remove(file)) || !files_.rename(file + ".bak", file)) return fail("Cannot recover playlist on microSD");
        if (!legacy) playlist.paths.file_ = file;
        return true;
    }
    return fail("Playlist not found or damaged");
}
bool Playlists::create(const std::string& name, uint32_t& id) {
    if (!files_.ready()) return fail("Insert a microSD card for playlists");
    for (uint32_t candidate = 1; candidate <= MaxLists; ++candidate) {
        auto file = path(candidate), legacy = file.substr(0, file.size() - 1);
        if (files_.exists(file) || files_.exists(file + ".bak") || files_.exists(legacy) || files_.exists(legacy + ".bak")) continue;
        Playlist playlist{candidate, name, {}};
        if (!save(playlist)) return false;
        id = candidate; return true;
    }
    return fail("Playlist limit reached (16)");
}
bool Playlists::save(const Playlist& playlist) {
    if (!files_.ready()) return fail("Insert a microSD card for playlists");
    if (!playlist.id || playlist.id > MaxLists) return fail("Invalid playlist ID");
    if (!validName(playlist.name) || playlist.paths.size() > MaxTracks) return fail("Invalid playlist: maximum 1000 unique tracks");
    for (const auto& item : list_) if (item.id != playlist.id && contains(item.name, playlist.name) && item.name.size() == playlist.name.size()) return fail("A playlist already has that name");
    auto file = path(playlist.id), temp = file + ".tmp", backup = file + ".bak";
    auto writer = files_.openWriter(temp);
    auto first = header(playlist);
    if (!writer || first.empty() || !writer->write(first)) return fail("Cannot write playlist; check microSD space");
    bool written = playlist.paths.each([&](size_t, const std::string& path) {
        if (!validMusicPath(path)) return false;
        auto data = jsonText(cJSON_CreateStringReference(path.c_str()));
        return !data.empty() && writer->write(data);
    });
    bool finished = writer->finish(); writer.reset();
    if (!written || !finished) return fail("Cannot write playlist; check microSD space");
    {
        Playlist checked; checked.id = playlist.id;
        if (!readFile(temp, checked) || checked.name != playlist.name || checked.paths.size() != playlist.paths.size()) return fail("Playlist write verification failed");
        auto source = playlist.paths.files_ ? files_.openReader(playlist.paths.file_) : nullptr;
        if (!checked.paths.each([&](size_t i, const std::string& actual) {
            std::string expected; return playlist.paths.read(source.get(), i, expected) && actual == expected;
        })) return fail("Playlist write verification failed");
    }
    if (files_.exists(backup) && !files_.remove(backup)) return fail("Cannot replace playlist backup");
    bool previous = files_.exists(file);
    if (previous && !files_.rename(file, backup)) return fail("Cannot back up playlist");
    if (!files_.rename(temp, file)) {
        if (previous) files_.rename(backup, file);
        return fail("Cannot finalize playlist; previous version retained");
    }
    files_.remove(backup);
    auto legacy = file.substr(0, file.size() - 1);
    files_.remove(legacy); files_.remove(legacy + ".bak");
    auto it = std::find_if(list_.begin(), list_.end(), [&](const PlaylistSummary& item) { return item.id == playlist.id; });
    PlaylistSummary summary{playlist.id, playlist.name, playlist.paths.size()};
    if (it == list_.end()) list_.push_back(summary); else *it = summary;
    std::sort(list_.begin(), list_.end(), [](const PlaylistSummary& a, const PlaylistSummary& b) { return a.id < b.id; });
    error_.clear(); return true;
}
bool Playlists::erase(uint32_t id) {
    Playlist playlist;
    if (!load(id, playlist)) return false;
    auto file = path(id), legacy = file.substr(0, file.size() - 1);
    for (const auto& candidate : {legacy + ".bak", legacy + ".tmp", legacy, file + ".bak", file + ".tmp", file}) {
        if (files_.exists(candidate) && !files_.remove(candidate)) return fail("Cannot delete playlist from microSD");
    }
    list_.erase(std::remove_if(list_.begin(), list_.end(), [&](const PlaylistSummary& item) { return item.id == id; }), list_.end());
    error_.clear(); return true;
}
}
