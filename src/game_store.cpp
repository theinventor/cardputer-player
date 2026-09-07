#include "games.h"
#include <cJSON.h>
#include <cmath>
#include <memory>
#include <algorithm>

namespace ct {
namespace merge_core { extern "C" bool gameEnded(uint8_t board[4][4]); }
namespace {
using Json = std::unique_ptr<cJSON, decltype(&cJSON_Delete)>;
constexpr const char* Path = "/.cardtunes/games-v1.json";
constexpr const char* Backup = "/.cardtunes/games-v1.bak";
constexpr const char* Temp = "/.cardtunes/games-v1.tmp";
constexpr size_t MaxBytes = 8192;
struct Numbers {
    cJSON* array;
    int index = 0;
    bool ok = true;
    void put(double n) {
        if (!ok) return;
        auto item = cJSON_CreateNumber(n);
        if (!item || !cJSON_AddItemToArray(array, item)) { cJSON_Delete(item); ok = false; }
    }
    double get(double low, double high, bool integer = true) {
        auto item = cJSON_GetArrayItem(array, index++);
        if (!cJSON_IsNumber(item) || !std::isfinite(item->valuedouble) || item->valuedouble < low || item->valuedouble > high || (integer && std::floor(item->valuedouble) != item->valuedouble)) { ok = false; return low; }
        return item->valuedouble;
    }
    bool done() const { return ok && cJSON_IsArray(array) && cJSON_GetArraySize(array) == index; }
};
}
std::string Games::serialize() const {
    Json root(cJSON_CreateObject(), cJSON_Delete);
    if (!root) return {};
    if (!cJSON_AddNumberToObject(root.get(), "version", 1)) return {};
    Numbers n{cJSON_AddArrayToObject(root.get(), "state")};
    if (!n.array) return {};
    for (int i = 0; i < 3; ++i) { n.put(initialized_[i]); n.put(best_[i]); }
    if (initialized_[0]) {
        const auto& f = falling;
        n.put(f.score); n.put(f.level); n.put(f.lines_cleared);
        n.put(f.shape_bag_idx); n.put(f.next_shape_kind);
        for (auto v : f.shape_bag) n.put(v);
        for (const auto& row : f.grid) for (auto v : row) n.put(v);
        for (auto v : f.current_shape) n.put(v);
        n.put(f.current_shape_kind); n.put(f.current_x); n.put(f.current_y);
        n.put(f.fall_elapsed_ms); n.put(f.fall_period_ms); n.put(f.game_over);
    }
    if (initialized_[1]) {
        const auto& b = breakout;
        n.put(b.x); n.put(b.y); n.put(b.vx); n.put(b.vy); n.put(b.paddle);
        for (auto v : b.bricks) n.put(v);
        n.put(b.score); n.put(b.level); n.put(b.lives); n.put(b.serving); n.put(b.over);
    }
    if (initialized_[2]) {
        for (const auto& col : merge.board) for (auto v : col) n.put(v);
        n.put(merge.score); n.put(merge.over); n.put(merge.won);
    }
    if (!n.ok) return {};
    char* raw = cJSON_PrintUnformatted(root.get());
    std::string result = raw ? raw : ""; cJSON_free(raw); return result;
}
bool Games::restore(const std::string& data) {
    if (data.empty() || data.size() > MaxBytes || data.find('\0') != std::string::npos) return false;
    // Saves contain only an object and one flat numeric array. Bound parser stack use.
    int depth = 0;
    bool quoted = false, escaped = false;
    for (char c : data) {
        if (quoted) {
            if (escaped) escaped = false;
            else if (c == '\\') escaped = true;
            else if (c == '"') quoted = false;
        } else if (c == '"') quoted = true;
        else if (c == '[' || c == '{') { if (++depth > 2) return false; }
        else if (c == ']' || c == '}') { if (--depth < 0) return false; }
    }
    if (quoted || depth != 0) return false;
    Json root(cJSON_ParseWithOpts(data.c_str(), nullptr, true), cJSON_Delete);
    auto version = cJSON_GetObjectItemCaseSensitive(root.get(), "version");
    if (!cJSON_IsObject(root.get()) || !cJSON_IsNumber(version) || version->valuedouble != 1) return false;
    Games next;
    Numbers n{cJSON_GetObjectItemCaseSensitive(root.get(), "state")};
    for (int i = 0; i < 3; ++i) { next.initialized_[i] = n.get(0, 1); next.best_[i] = n.get(0, UINT32_MAX); }
    if (next.initialized_[0]) {
        auto& f = next.falling;
        f.score = n.get(0, UINT32_MAX); f.level = n.get(0, UINT32_MAX / 10); f.lines_cleared = n.get(0, UINT32_MAX);
        f.shape_bag_idx = n.get(0, 6); f.next_shape_kind = n.get(0, 6);
        unsigned bag = 0;
        for (auto& v : f.shape_bag) { v = n.get(0, 6); bag |= 1u << v; }
        if (bag != 127 || f.level != f.lines_cleared / 10) return false;
        for (auto& row : f.grid) for (auto& v : row) v = n.get(0, 7);
        for (auto& v : f.current_shape) v = n.get(-2, 2);
        f.current_shape_kind = n.get(0, 6); f.current_x = n.get(0, 9); f.current_y = n.get(-2, 19);
        f.fall_elapsed_ms = n.get(0, 799); f.fall_period_ms = n.get(20, 800); f.game_over = n.get(0, 1);
        int canonical[8]; blocks::game_shape(f.current_shape_kind, canonical);
        bool validShape = false;
        for (int rotation = 0; rotation < (f.current_shape_kind == 0 ? 1 : 4); ++rotation) {
            validShape |= std::equal(canonical, canonical + 8, f.current_shape);
            for (int i = 0; i < 4; ++i) { int x = canonical[i * 2]; canonical[i * 2] = canonical[i * 2 + 1]; canonical[i * 2 + 1] = -x; }
        }
        if (!validShape) return false;
        for (int i = 0; i < 4; ++i) {
            int x = f.current_x + f.current_shape[i * 2], y = f.current_y + f.current_shape[i * 2 + 1];
            if (x < 0 || x >= 10 || y < -4 || y >= 20 || (!f.game_over && y >= 0 && f.grid[y][x])) return false;
            for (int j = 0; j < i; ++j) if (f.current_shape[i * 2] == f.current_shape[j * 2] && f.current_shape[i * 2 + 1] == f.current_shape[j * 2 + 1]) return false;
        }
        f.changed = 1;
    }
    if (next.initialized_[1]) {
        auto& b = next.breakout;
        b.x = n.get(3, 237, false); b.y = n.get(18, 141, false); b.vx = n.get(-240, 240, false); b.vy = n.get(-240, 240, false); b.paddle = n.get(22, 218, false);
        unsigned bricks = 0;
        for (auto& v : b.bricks) { v = n.get(0, 1); bricks += v; }
        b.score = n.get(0, UINT32_MAX); b.level = n.get(1, 999); b.lives = n.get(0, 3); b.serving = n.get(0, 1); b.over = n.get(0, 1);
        if (!bricks || b.over != (b.lives == 0) || (!b.serving && b.vx == 0 && b.vy == 0)) return false;
    }
    if (next.initialized_[2]) {
        bool won = false; unsigned count = 0;
        for (auto& col : next.merge.board) for (auto& v : col) { v = n.get(0, 11); won |= v == 11; count += v != 0; }
        next.merge.score = n.get(0, UINT32_MAX); next.merge.over = n.get(0, 1); next.merge.won = n.get(0, 1);
        if (!count || won != next.merge.won || next.merge.over != merge_core::gameEnded(next.merge.board)) return false;
    }
    if (!n.done()) return false;
    for (int i = 1; i <= 3; ++i) if (next.best(GameId(i)) < next.score(GameId(i))) return false;
    *this = next; return true;
}
bool GameStore::load(Games& games) {
    error_.clear();
    restored_ = false;
    if (!files_.ready()) { error_ = "No SD: games save in RAM only"; return false; }
    std::string data;
    for (const char* path : {Path, Backup}) if (files_.read(path, data, MaxBytes) && games.restore(data)) { restored_ = true; return true; }
    if (files_.exists(Path) || files_.exists(Backup)) { error_ = "Game save unreadable"; return false; }
    restored_ = true; return true;
}
bool GameStore::save(Games& games) {
    if (!games.dirty()) return true;
    error_.clear();
    if (!files_.ready()) { error_ = "No SD: games save in RAM only"; return false; }
    if (!restored_) { error_ = "Rescan to restore SD game saves"; return false; }
    std::string data = games.serialize(), checked;
    Games verify;
    if (data.empty() || !verify.restore(data) || !files_.write(Temp, data) || !files_.read(Temp, checked, MaxBytes) || checked != data) { error_ = "Cannot write game save"; return false; }
    // Retain the last valid generation even when recovering a damaged primary.
    if (files_.exists(Path)) {
        std::string old;
        if (files_.read(Path, old, MaxBytes) && verify.restore(old)) {
            if ((files_.exists(Backup) && !files_.remove(Backup)) || !files_.rename(Path, Backup)) { error_ = "Cannot rotate game save"; return false; }
        } else if (!files_.remove(Path)) { error_ = "Cannot replace game save"; return false; }
    }
    if (!files_.rename(Temp, Path)) { error_ = "Cannot finalize game save"; return false; }
    games.saved(); return true;
}
}
