#include "playlists.h"
#include "media.h"
#include <cJSON.h>
#include <algorithm>
#include <cctype>
#include <cstring>

namespace ct {
bool Playlists::validName(const std::string& name) {
    if (name.empty() || name.size() > 63 || std::isspace(uint8_t(name.front())) || std::isspace(uint8_t(name.back()))) return false;
    return std::none_of(name.begin(), name.end(), [](unsigned char c) { return c < 32 || c == 127; });
}
bool Playlists::decode(const std::string& data, Playlist& playlist) {
    if (data.size() > MaxBytes || data.find('\0') != std::string::npos) return false;
    cJSON* json = cJSON_ParseWithOpts(data.c_str(), nullptr, true);
    if (!json) return false;
    auto version = cJSON_GetObjectItemCaseSensitive(json, "version");
    auto name = cJSON_GetObjectItemCaseSensitive(json, "name");
    auto paths = cJSON_GetObjectItemCaseSensitive(json, "paths");
    bool ok = cJSON_IsObject(json) && cJSON_IsNumber(version) && version->valuedouble == 1 &&
        cJSON_IsString(name) && validName(name->valuestring) && cJSON_IsArray(paths) && cJSON_GetArraySize(paths) <= int(MaxTracks);
    Playlist result;
    if (ok) {
        result.name = name->valuestring;
        cJSON* item;
        cJSON_ArrayForEach(item, paths) {
            if (!cJSON_IsString(item) || !validMusicPath(item->valuestring) || std::find(result.paths.begin(), result.paths.end(), item->valuestring) != result.paths.end()) { ok = false; break; }
            result.paths.emplace_back(item->valuestring);
        }
    }
    cJSON_Delete(json);
    if (ok) { result.id = playlist.id; playlist = std::move(result); }
    return ok;
}
std::string Playlists::encode(const Playlist& playlist) {
    if (!validName(playlist.name) || playlist.paths.size() > MaxTracks) return {};
    auto json = cJSON_CreateObject();
    if (!json) return {};
    bool ok = cJSON_AddNumberToObject(json, "version", 1) && cJSON_AddStringToObject(json, "name", playlist.name.c_str());
    auto paths = cJSON_AddArrayToObject(json, "paths");
    ok = ok && paths;
    size_t capacity = 72 + playlist.name.size() * 2;
    for (auto it = playlist.paths.begin(); it != playlist.paths.end(); ++it) {
        const auto& path = *it;
        if (!ok || !validMusicPath(path) || std::find(playlist.paths.begin(), it, path) != it) { ok = false; break; }
        auto item = cJSON_CreateStringReference(path.c_str());
        if (!item) { ok = false; break; }
        if (!cJSON_AddItemToArray(paths, item)) { cJSON_Delete(item); ok = false; break; }
        capacity += path.size() * 2 + 4;
    }
    // Reuse the result buffer and existing paths: no second full JSON copy on the MCU heap.
    std::string data(ok ? std::min<size_t>(MaxBytes + 8, capacity) : 0, '\0');
    if (ok && cJSON_PrintPreallocated(json, &data[0], data.size(), false)) data.resize(strlen(data.c_str()));
    else data.clear();
    cJSON_Delete(json);
    return data.size() <= MaxBytes ? data : std::string();
}
std::string Playlists::path(uint32_t id) { return "/.cardtunes/playlist-" + std::to_string(id) + ".json"; }
bool Playlists::begin() {
    list_.clear(); error_.clear();
    if (!files_.ready()) return fail("Insert a microSD card for playlists");
    bool ok = true;
    for (uint32_t id = 1; id <= MaxLists; ++id) {
        if (!files_.exists(path(id)) && !files_.exists(path(id) + ".bak")) continue;
        Playlist playlist;
        if (load(id, playlist)) list_.push_back({id, playlist.name, playlist.paths.size()});
        else ok = false;
    }
    if (!ok) return fail("A playlist could not be read; its file was preserved");
    return true;
}
bool Playlists::load(uint32_t id, Playlist& playlist) {
    if (!files_.ready()) return fail("Insert a microSD card for playlists");
    if (!id || id > MaxLists) return fail("Invalid playlist ID");
    std::string data, file = path(id);
    playlist.id = id;
    if (files_.read(file, data, MaxBytes) && decode(data, playlist)) return true;
    if (files_.read(file + ".bak", data, MaxBytes) && decode(data, playlist)) {
        // A power loss between the two renames leaves the last good version here.
        if ((files_.exists(file) && !files_.remove(file)) || !files_.rename(file + ".bak", file)) return fail("Cannot recover playlist on microSD");
        return true;
    }
    return fail("Playlist not found or damaged");
}
bool Playlists::create(const std::string& name, uint32_t& id) {
    if (!files_.ready()) return fail("Insert a microSD card for playlists");
    for (uint32_t candidate = 1; candidate <= MaxLists; ++candidate) {
        if (files_.exists(path(candidate)) || files_.exists(path(candidate) + ".bak")) continue;
        Playlist playlist{candidate, name, {}};
        if (!save(playlist)) return false;
        id = candidate; return true;
    }
    return fail("Playlist limit reached (16)");
}
bool Playlists::save(const Playlist& playlist) {
    if (!files_.ready()) return fail("Insert a microSD card for playlists");
    if (!playlist.id || playlist.id > MaxLists) return fail("Invalid playlist ID");
    for (const auto& item : list_) if (item.id != playlist.id && contains(item.name, playlist.name) && item.name.size() == playlist.name.size()) return fail("A playlist already has that name");
    auto data = encode(playlist);
    if (data.empty()) return fail("Invalid playlist: use a name up to 63 bytes and at most 128 unique tracks");
    auto file = path(playlist.id), temp = file + ".tmp", backup = file + ".bak";
    if (!files_.write(temp, data)) return fail("Cannot write playlist; check microSD space");
    std::string check;
    if (!files_.read(temp, check, MaxBytes) || check != data) return fail("Playlist write verification failed");
    if (files_.exists(backup) && !files_.remove(backup)) return fail("Cannot replace playlist backup");
    bool previous = files_.exists(file);
    if (previous && !files_.rename(file, backup)) return fail("Cannot back up playlist");
    if (!files_.rename(temp, file)) {
        if (previous) files_.rename(backup, file);
        return fail("Cannot finalize playlist; previous version retained");
    }
    files_.remove(backup);
    auto it = std::find_if(list_.begin(), list_.end(), [&](const PlaylistSummary& item) { return item.id == playlist.id; });
    PlaylistSummary summary{playlist.id, playlist.name, playlist.paths.size()};
    if (it == list_.end()) list_.push_back(summary); else *it = summary;
    std::sort(list_.begin(), list_.end(), [](const PlaylistSummary& a, const PlaylistSummary& b) { return a.id < b.id; });
    error_.clear(); return true;
}
bool Playlists::erase(uint32_t id) {
    Playlist playlist;
    if (!load(id, playlist)) return false;
    auto file = path(id);
    for (const auto& candidate : {file + ".bak", file + ".tmp", file}) {
        if (files_.exists(candidate) && !files_.remove(candidate)) return fail("Cannot delete playlist from microSD");
    }
    list_.erase(std::remove_if(list_.begin(), list_.end(), [&](const PlaylistSummary& item) { return item.id == id; }), list_.end());
    error_.clear(); return true;
}
}
