#include "web.h"
#include "web_page.h"
#include <Update.h>

namespace ct {
bool Web::authorized() {
    String header = server_.header("Authorization");
    String expected = "Bearer " + app_.settings.token;
    unsigned difference = header.length() ^ expected.length();
    for (size_t i = 0; i < expected.length(); ++i) difference |= uint8_t(i < header.length() ? header[i] : 0) ^ uint8_t(expected[i]);
    return difference == 0;
}
void Web::error(int code, const String& message) {
    auto json = cJSON_CreateObject(); cJSON_AddStringToObject(json, "error", message.c_str());
    server_.send(code, "application/json", jsonString(json));
}
void Web::begin() {
    const char* headers[] = {"Authorization"}; server_.collectHeaders(headers, 1);
    server_.on("/", HTTP_GET, [&] { server_.sendHeader("Cache-Control", "no-cache"); server_.send_P(200, "text/html; charset=utf-8", WebPage); });
    server_.on("/api/status", HTTP_GET, [&] {
        if (!authorized()) { error(401, "Pair with the access key from device Settings"); return; }
        server_.sendHeader("Cache-Control", "no-store"); server_.send(200, "application/json", jsonString(app_.status()));
    });
    server_.on("/api/library", HTTP_GET, [&] {
        if (!authorized()) { error(401, "Access key required"); return; }
        uint32_t offset = 0, limit = 32;
        if ((server_.hasArg("offset") && !parseNumber(server_.arg("offset"), app_.library.count(), offset)) ||
            (server_.hasArg("limit") && (!parseNumber(server_.arg("limit"), 64, limit) || !limit))) { error(400, "Invalid page"); return; }
        if (server_.arg("q").length() > 64) { error(400, "Search is too long"); return; }
        auto json = cJSON_CreateObject(); auto tracks = cJSON_AddArrayToObject(json, "tracks");
        String query = server_.arg("q");
        int id = app_.library.find(query.c_str(), offset);
        unsigned count = 0;
        while (id >= 0 && count++ < limit) { cJSON_AddItemToArray(tracks, app_.trackJson(id)); id = app_.library.find(query.c_str(), id + 1); }
        cJSON_AddNumberToObject(json, "next_offset", id);
        cJSON_AddNumberToObject(json, "total", app_.library.count());
        server_.send(200, "application/json", jsonString(json));
    });
    server_.on("/api/queue", HTTP_GET, [&] {
        if (!authorized()) { error(401, "Access key required"); return; }
        auto json = cJSON_CreateObject(); auto tracks = cJSON_AddArrayToObject(json, "tracks");
        for (auto id : app_.order.queue()) cJSON_AddItemToArray(tracks, app_.trackJson(id));
        server_.send(200, "application/json", jsonString(json));
    });
    server_.on("/api/playlists", HTTP_GET, [&] {
        if (!authorized()) { error(401, "Access key required"); return; }
        uint32_t id = 0, offset = 0, limit = 32;
        if ((server_.hasArg("id") && (!parseNumber(server_.arg("id"), Playlists::MaxLists, id) || !id)) ||
            (server_.hasArg("offset") && !parseNumber(server_.arg("offset"), Playlists::MaxTracks, offset)) ||
            (server_.hasArg("limit") && (!parseNumber(server_.arg("limit"), 32, limit) || !limit))) { error(400, "Invalid playlist page"); return; }
        auto json = app_.playlistJson(id, offset, limit);
        if (!json) { error(404, app_.playlists.error().c_str()); return; }
        server_.sendHeader("Cache-Control", "no-store");
        server_.send(200, "application/json", jsonString(json));
    });
    server_.on("/api/playlists", HTTP_POST, [&] {
        if (!authorized()) { error(401, "Access key required"); return; }
        uint32_t id = 0, to = 0;
        String action = server_.arg("action"), value = server_.arg("value"), message;
        if (value.length() > 63 || (action != "create" && (!parseNumber(server_.arg("id"), Playlists::MaxLists, id) || !id)) ||
            (action == "move" && !parseNumber(server_.arg("to"), Playlists::MaxTracks - 1, to))) { error(400, "Invalid playlist parameters"); return; }
        if (!app_.editPlaylist(action, id, value, to, message)) { error(400, message); return; }
        auto json = cJSON_CreateObject(); cJSON_AddBoolToObject(json, "ok", true); cJSON_AddNumberToObject(json, "id", id);
        server_.send(action == "create" ? 201 : 200, "application/json", jsonString(json));
    });
    server_.on("/api/control", HTTP_POST, [&] {
        if (!authorized()) { error(401, "Access key required"); return; }
        String message;
        if (server_.arg("action") == "playlist" && server_.hasArg("track")) {
            uint32_t id = 0, track = 0;
            if ((server_.arg("value") != "all" && (!parseNumber(server_.arg("value"), Playlists::MaxLists, id) || !id)) ||
                !parseNumber(server_.arg("track"), Library::MaxTracks, track)) { error(400, "Invalid playlist track"); return; }
            if (!app_.selectPlaylist(id, true, message, track)) { error(400, message); return; }
            server_.send(202, "application/json", "{\"ok\":true}"); return;
        }
        if (!app_.control(server_.arg("action"), server_.arg("value"), message)) { error(400, message.isEmpty() ? "Command unavailable" : message); return; }
        server_.send(202, "application/json", "{\"ok\":true}");
    });
    server_.on("/api/input", HTTP_POST, [&] {
        if (!authorized()) { error(401, "Access key required"); return; }
        String key = server_.arg("key");
        if (key.length() > 12) { error(400, "Invalid key"); return; }
        display_.key(key); server_.send(200, "application/json", "{\"ok\":true}");
    });
    server_.on("/api/screen.bmp", HTTP_GET, [&] { if (!authorized()) error(401, "Access key required"); else screenshot(); });
    server_.on("/api/cover", HTTP_GET, [&] { if (!authorized()) error(401, "Access key required"); else cover(); });
    server_.on("/api/upload", HTTP_POST, [&] { finishUpload(); }, [&] { uploadChunk(false); });
    server_.on("/api/firmware", HTTP_POST, [&] { finishUpload(); }, [&] { uploadChunk(true); });
    server_.onNotFound([&] { error(404, "Not found"); });
    server_.begin();
}
void Web::tick() {
    if (WiFi.isConnected()) server_.handleClient();
    if (rebootAt_ && int32_t(millis() - rebootAt_) >= 0) ESP.restart();
}
void Web::uploadChunk(bool firmware) {
    auto& upload = server_.upload();
    if (upload.status == UPLOAD_FILE_START) {
        uploadAllowed_ = false; firmware_ = firmware; uploadError_ = ""; received_ = 0; uploadStatus_ = 400;
        if (!authorized()) { uploadStatus_ = 401; uploadError_ = "Access key required"; return; }
        if (app_.library.scanning()) { uploadStatus_ = 409; uploadError_ = "Cancel the music scan before uploading"; return; }
        if (app_.games.active() != GameId::None) { uploadStatus_ = 409; uploadError_ = "Exit the game before uploading"; return; }
        if (!parseNumber(server_.arg("size"), firmware ? 0x300000 : 256 * 1024 * 1024, expected_) || !expected_) { uploadError_ = "A valid size query parameter is required"; return; }
        if (!firmware && !app_.library.mounted()) { uploadStatus_ = 503; uploadError_ = "Insert a microSD card and rescan before uploading music"; return; }
        if (!firmware) {
            uploadPath_ = server_.hasArg("path") ? server_.arg("path") : String(ct::uploadPath(upload.filename.c_str()).c_str());
            if (!validMusicPath(uploadPath_.c_str()) || !uploadPath_.startsWith("/Music/") || uploadPath_.indexOf('/', 191) >= 0) { uploadError_ = "Invalid music path"; return; }
            if (app_.library.count() >= Library::MaxTracks) { uploadError_ = "Library limit reached"; return; }
            SDLock lock(sdMutex);
            if (SD.exists(uploadPath_)) { uploadStatus_ = 409; uploadError_ = "File already exists"; return; }
            if (SD.totalBytes() - SD.usedBytes() < expected_ + 65536) { uploadStatus_ = 507; uploadError_ = "Not enough SD space"; return; }
        }
        if (!(firmware ? app_.audio.stopAndWait() : app_.audio.pauseAndWait())) { uploadStatus_ = 503; uploadError_ = "Player is busy; retry shortly"; return; }
        app_.saveResume();
        if (firmware) {
            if (!Update.begin(expected_, U_FLASH)) { uploadError_ = Update.errorString(); return; }
        } else {
            SDLock lock(sdMutex);
            for (int slash = uploadPath_.indexOf('/', 1); slash >= 0; slash = uploadPath_.indexOf('/', slash + 1)) {
                String directory = uploadPath_.substring(0, slash);
                if (!SD.exists(directory) && !SD.mkdir(directory)) { uploadError_ = "Cannot create music folder"; return; }
            }
            upload_ = SD.open("/Music/.upload.part", FILE_WRITE);
            if (!upload_) { uploadError_ = "Cannot write to SD card"; return; }
        }
        uploadAllowed_ = true;
    } else if (upload.status == UPLOAD_FILE_WRITE && uploadAllowed_) {
        if (upload.currentSize > expected_ - received_) { uploadError_ = "Upload exceeds declared size"; uploadAllowed_ = false; return; }
        size_t written;
        if (firmware_) written = Update.write(upload.buf, upload.currentSize);
        else { SDLock lock(sdMutex); written = upload_.write(upload.buf, upload.currentSize); }
        if (written != upload.currentSize) { uploadError_ = "Write failed"; uploadAllowed_ = false; }
        received_ += written;
        // A continuous multipart body must still leave time for scheduler work.
        delay(1);
    } else if (upload.status == UPLOAD_FILE_ABORTED) {
        uploadAllowed_ = false; uploadError_ = "Upload interrupted";
        if (firmware_) Update.abort(); else { SDLock lock(sdMutex); upload_.close(); SD.remove("/Music/.upload.part"); }
    } else if (upload.status == UPLOAD_FILE_END) {
        if (received_ != expected_) { uploadAllowed_ = false; if (uploadError_.isEmpty()) uploadError_ = "Incomplete upload"; }
        if (!firmware_) { SDLock lock(sdMutex); upload_.flush(); upload_.close(); }
    }
}
void Web::finishUpload() {
    if (!authorized()) { error(401, "Access key required"); return; }
    if (!uploadAllowed_ || received_ != expected_ || !expected_) {
        if (firmware_) Update.abort();
        else if (app_.library.mounted()) { SDLock lock(sdMutex); upload_.close(); SD.remove("/Music/.upload.part"); }
        error(uploadStatus_, uploadError_.isEmpty() ? "Missing upload" : uploadError_); return;
    }
    uploadAllowed_ = false;
    if (firmware_) {
        if (!Update.end()) { error(400, Update.errorString()); return; }
        server_.send(200, "application/json", "{\"ok\":true,\"rebooting\":true}"); rebootAt_ = millis() + 600; return;
    }
    {
        SDLock lock(sdMutex);
        if (!SD.rename("/Music/.upload.part", uploadPath_)) { error(500, "Cannot finalize upload"); return; }
    }
    if (!app_.library.append(uploadPath_.c_str())) { error(500, "File saved but indexing failed; rescan the library"); return; }
    app_.order.grow(app_.library.count());
    auto json = cJSON_CreateObject(); cJSON_AddBoolToObject(json, "ok", true); cJSON_AddStringToObject(json, "path", uploadPath_.c_str());
    cJSON_AddNumberToObject(json, "bytes", received_);
    server_.send(201, "application/json", jsonString(json));
}
void Web::screenshot() {
    display_.render();
    constexpr uint32_t size = 54 + 240 * 135 * 3;
    uint8_t header[54]{};
    auto little32 = [&](unsigned offset, uint32_t value) { for (unsigned i = 0; i < 4; ++i) header[offset + i] = value >> (i * 8); };
    header[0] = 'B'; header[1] = 'M'; little32(2, size); little32(10, 54); little32(14, 40);
    little32(18, 240); little32(22, 135); header[26] = 1; header[28] = 24;
    server_.setContentLength(size); server_.send(200, "image/bmp", "");
    auto client = server_.client(); client.write(header, sizeof(header));
    uint8_t row[240 * 3];
    for (int y = 134; y >= 0 && client.connected(); --y) {
        for (int x = 0; x < 240; ++x) {
            auto color = display_.canvas().readPixelRGB(x, y);
            row[x * 3] = color.B8(); row[x * 3 + 1] = color.G8(); row[x * 3 + 2] = color.R8();
        }
        if (client.write(row, sizeof(row)) != sizeof(row)) break;
        delay(1);
    }
}
void Web::cover() {
    String path = app_.currentTrack.path;
    SDLock lock(sdMutex);
    File file;
    if (path == "/@demo.mp3") file = LittleFS.open("/demo.jpg", FILE_READ);
    else if (!path.isEmpty()) { path = path.substring(0, path.lastIndexOf('/') + 1) + "cover.jpg"; file = SD.open(path, FILE_READ); }
    if (!file || file.size() > 262144) { error(404, "No artwork"); return; }
    server_.streamFile(file, "image/jpeg"); file.close();
}
}
