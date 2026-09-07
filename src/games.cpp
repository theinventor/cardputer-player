#include "games.h"
#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cstring>
#define CUTE_C2_IMPLEMENTATION
#include "third_party/cute_c2.h"

namespace ct {
namespace merge_core {
extern "C" {
bool moveLeft(uint8_t board[4][4], uint32_t* score);
bool moveRight(uint8_t board[4][4], uint32_t* score);
bool moveUp(uint8_t board[4][4], uint32_t* score);
bool moveDown(uint8_t board[4][4], uint32_t* score);
bool gameEnded(uint8_t board[4][4]);
}
}
const char* gameName(GameId id) {
    switch (id) {
    case GameId::Blocks: return "Falling Blocks";
    case GameId::Breakout: return "Breakout";
    case GameId::Merge2048: return "2048";
    default: return "Games";
    }
}
const char* gameSlug(GameId id) {
    switch (id) {
    case GameId::Blocks: return "blocks";
    case GameId::Breakout: return "breakout";
    case GameId::Merge2048: return "2048";
    default: return "none";
    }
}
GameId gameId(const std::string& name) {
    for (int i = 1; i <= 3; ++i) if (name == gameSlug(GameId(i))) return GameId(i);
    return GameId::None;
}
GameKey gameKey(const std::string& key) {
    if (key == "a" || key == "," || key == "left") return GameKey::Left;
    if (key == "d" || key == "/" || key == "right") return GameKey::Right;
    if (key == "w" || key == ";" || key == "up") return GameKey::Up;
    if (key == "s" || key == "." || key == "down") return GameKey::Down;
    if (key == "enter" || key == " " || key == "primary") return GameKey::Primary;
    if (key == "p" || key == "tab" || key == "pause") return GameKey::Pause;
    if (key == "`" || key == "escape" || key == "exit") return GameKey::Exit;
    return GameKey::None;
}
bool Games::hasSave(GameId id) const { return id >= GameId::Blocks && id <= GameId::Merge2048 && initialized_[int(id) - 1]; }
uint32_t Games::score(GameId id) const {
    if (!hasSave(id)) return 0;
    return id == GameId::Blocks ? falling.score : id == GameId::Breakout ? breakout.score : merge.score;
}
uint32_t Games::best(GameId id) const { return id >= GameId::Blocks && id <= GameId::Merge2048 ? best_[int(id) - 1] : 0; }
bool Games::over() const {
    return active_ == GameId::Blocks ? falling.game_over : active_ == GameId::Breakout ? breakout.over : active_ == GameId::Merge2048 && (merge.over || merge.won);
}
void Games::updateBest() {
    if (active_ != GameId::None) best_[int(active_) - 1] = std::max(best(active_), score(active_));
}
bool Games::start(GameId id) {
    if (id < GameId::Blocks || id > GameId::Merge2048) return false;
    ++revision_;
    active_ = id; accumulated_ = 0; left_ = right_ = false; paused_ = false; menu_ = 0;
    if (!hasSave(id)) restart();
    if (over()) { paused_ = true; menu_ = 1; }
    return true;
}
void Games::leave() { updateBest(); ++revision_; active_ = GameId::None; paused_ = left_ = right_ = false; accumulated_ = 0; }
void Games::pause(bool value) { ++revision_; paused_ = value; left_ = right_ = false; accumulated_ = 0; menu_ = over() ? 1 : 0; }
void Games::restart() {
    if (active_ == GameId::None) return;
    ++revision_;
    updateBest();
    if (active_ == GameId::Blocks) falling = blocks::game_state_new();
    else if (active_ == GameId::Breakout) { breakout = BreakoutState{}; breakout.bricks.fill(1); }
    else { merge = MergeState{}; randomTile(); randomTile(); }
    initialized_[int(active_) - 1] = true;
    paused_ = left_ = right_ = false; accumulated_ = 0; menu_ = 0; dirty_ = true;
}
void Games::randomTile() {
    uint8_t slots[16], count = 0;
    for (int x = 0; x < 4; ++x) for (int y = 0; y < 4; ++y) if (!merge.board[x][y]) slots[count++] = x * 4 + y;
    if (count) { int at = slots[std::rand() % count]; merge.board[at / 4][at % 4] = std::rand() % 10 == 0 ? 2 : 1; }
}
bool Games::input(GameKey key) {
    if (active_ == GameId::None) return true;
    if (key == GameKey::None) return true;
    ++revision_;
    if (key == GameKey::Exit) return false;
    if (key == GameKey::Pause) { pause(!paused_ || over()); return true; }
    if (paused_ || over()) {
        if (key == GameKey::Up) menu_ = std::max(over() ? 1 : 0, menu_ - 1);
        if (key == GameKey::Down) menu_ = std::min(2, menu_ + 1);
        if (key == GameKey::Primary) {
            if (menu_ == 2) return false;
            if (menu_ == 1) restart(); else pause(false);
        }
        return true;
    }
    if (active_ == GameId::Blocks) {
        auto event = blocks::NO_INPUT;
        if (key == GameKey::Left) event = blocks::LEFT;
        if (key == GameKey::Right) event = blocks::RIGHT;
        if (key == GameKey::Up) event = blocks::ROTATE;
        if (key == GameKey::Down) { falling.fall_elapsed_ms = falling.fall_period_ms; }
        if (key == GameKey::Primary) event = blocks::HARD_DROP;
        blocks::game_state_update(&falling, event, 0);
    } else if (active_ == GameId::Breakout) {
        if (key == GameKey::Left) breakout.paddle = std::max(22.0f, breakout.paddle - 9);
        if (key == GameKey::Right) breakout.paddle = std::min(218.0f, breakout.paddle + 9);
        if (key == GameKey::Primary && breakout.serving) {
            breakout.serving = false;
            float speed = std::min(240.0f, 105.0f + 12 * breakout.level);
            breakout.vx = speed * 0.45f; breakout.vy = -speed * 0.893f;
        }
    } else {
        bool changed = false;
        if (key == GameKey::Left) changed = merge_core::moveLeft(merge.board, &merge.score);
        if (key == GameKey::Right) changed = merge_core::moveRight(merge.board, &merge.score);
        if (key == GameKey::Up) changed = merge_core::moveUp(merge.board, &merge.score);
        if (key == GameKey::Down) changed = merge_core::moveDown(merge.board, &merge.score);
        if (changed) {
            randomTile();
            for (const auto& col : merge.board) for (auto tile : col) if (tile >= 11) merge.won = true;
            merge.over = merge_core::gameEnded(merge.board);
        }
        if (key == GameKey::Primary) pause();
    }
    dirty_ = true; updateBest();
    if (over()) pause();
    return true;
}
void Games::tick(uint32_t elapsedMs) {
    if (active_ == GameId::None || paused_ || over()) return;
    // Bound catch-up after an HTTP transfer or screen lock; never fast-forward a game.
    accumulated_ += std::min<uint32_t>(elapsedMs, 50);
    while (accumulated_ >= 8) {
        accumulated_ -= 8;
        if (active_ == GameId::Blocks) blocks::game_state_update(&falling, blocks::NO_INPUT, 8);
        else if (active_ == GameId::Breakout) stepBreakout(0.008f);
        else break;
        ++revision_;
        dirty_ = true;
        if (over()) { pause(); break; }
    }
    if (active_ == GameId::Merge2048) accumulated_ = 0;
    updateBest();
}
void Games::mergeSaved(const Games& card) {
    // On late SD insertion the card's existing sessions win; retain RAM-only games and bests.
    if (card.hasSave(GameId::Blocks)) falling = card.falling;
    if (card.hasSave(GameId::Breakout)) breakout = card.breakout;
    if (card.hasSave(GameId::Merge2048)) merge = card.merge;
    for (int i = 0; i < 3; ++i) {
        initialized_[i] = initialized_[i] || card.initialized_[i];
        best_[i] = std::max(best_[i], card.best_[i]);
    }
    ++revision_;
}
void Games::stepBreakout(float seconds) {
    auto& b = breakout;
    b.paddle = std::clamp(b.paddle + (int(right_) - int(left_)) * 180 * seconds, 22.0f, 218.0f);
    if (b.serving) { b.x = b.paddle; b.y = 119; return; }
    b.x += b.vx * seconds; b.y += b.vy * seconds;
    auto collide = [&](c2AABB box) {
        c2Circle ball{c2V(b.x, b.y), 2};
        c2Manifold hit{}; c2CircletoAABBManifold(ball, box, &hit);
        if (!hit.count) return false;
        b.x -= hit.n.x * (hit.depths[0] + 0.05f); b.y -= hit.n.y * (hit.depths[0] + 0.05f);
        float toward = b.vx * hit.n.x + b.vy * hit.n.y;
        if (toward > 0) { b.vx -= 2 * toward * hit.n.x; b.vy -= 2 * toward * hit.n.y; }
        return true;
    };
    collide({c2V(-10, -10), c2V(3, 150)});
    collide({c2V(237, -10), c2V(250, 150)});
    collide({c2V(0, 0), c2V(240, 18)});
    if (b.vy > 0 && b.y < 128 && collide({c2V(b.paddle - 18, 123), c2V(b.paddle + 18, 127)})) {
        float speed = std::min(240.0f, 105.0f + 12 * b.level);
        b.vx = std::clamp((b.x - b.paddle) / 18, -0.9f, 0.9f) * speed;
        b.vy = -std::sqrt(speed * speed - b.vx * b.vx);
    }
    for (int i = 0; i < 40; ++i) {
        if (!b.bricks[i]) continue;
        int x = 9 + (i % 8) * 28, y = 29 + (i / 8) * 9;
        if (collide({c2V(x, y), c2V(x + 26, y + 6)})) { b.bricks[i] = 0; b.score += 10; break; }
    }
    if (b.y > 138) { --b.lives; b.over = !b.lives; b.serving = true; b.vx = b.vy = 0; b.x = b.paddle; b.y = 119; }
    if (std::none_of(b.bricks.begin(), b.bricks.end(), [](uint8_t brick) { return brick != 0; })) {
        b.level = std::min<uint32_t>(999, b.level + 1); b.bricks.fill(1); b.serving = true;
        b.x = b.paddle; b.y = 119; b.vx = b.vy = 0;
    }
}
}
