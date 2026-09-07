#pragma once
#include <array>
#include <cstdint>
#include <string>
#include "playlists.h"

namespace ct {
namespace blocks {
extern "C" {
#include "third_party/blocks/game.h"
}
}
enum class GameId { None, Blocks, Breakout, Merge2048 };
enum class GameKey { None, Left, Right, Up, Down, Primary, Pause, Exit };
const char* gameName(GameId id);
const char* gameSlug(GameId id);
GameId gameId(const std::string& name);
GameKey gameKey(const std::string& key);
struct BreakoutState {
    float x = 120, y = 119, vx = 0, vy = 0, paddle = 120;
    std::array<uint8_t, 40> bricks{};
    uint32_t score = 0, level = 1, lives = 3;
    bool serving = true, over = false;
};
struct MergeState {
    uint8_t board[4][4]{};
    uint32_t score = 0;
    bool over = false, won = false;
};
class Games {
public:
    GameId active() const { return active_; }
    bool paused() const { return paused_; }
    bool over() const;
    bool won() const { return active_ == GameId::Merge2048 && merge.won; }
    int menuSelection() const { return menu_; }
    bool hasSave(GameId id) const;
    uint32_t score(GameId id) const;
    uint32_t best(GameId id) const;
    bool start(GameId id);
    void leave();
    void restart();
    void pause(bool paused = true);
    bool input(GameKey key); // false requests exit back to music
    void held(bool left, bool right) { left_ = left; right_ = right; }
    void tick(uint32_t elapsedMs);
    bool dirty() const { return dirty_; }
    uint32_t revision() const { return revision_; }
    void saved() { dirty_ = false; }
    void mergeSaved(const Games& card);
    std::string serialize() const;
    bool restore(const std::string& json);
    blocks::game_state_t falling{};
    BreakoutState breakout;
    MergeState merge;
private:
    GameId active_ = GameId::None;
    bool paused_ = false, left_ = false, right_ = false, dirty_ = false;
    int menu_ = 0;
    uint32_t accumulated_ = 0;
    uint32_t revision_ = 0;
    std::array<bool, 3> initialized_{};
    std::array<uint32_t, 3> best_{};
    void stepBreakout(float seconds);
    void randomTile();
    void updateBest();
};
class GameStore {
public:
    explicit GameStore(PlaylistFiles& files) : files_(files) {}
    bool load(Games& games);
    bool save(Games& games);
    bool restored() const { return restored_; }
    const std::string& error() const { return error_; }
private:
    PlaylistFiles& files_;
    std::string error_;
    bool restored_ = false;
};
class GamePainter {
public:
    virtual ~GamePainter() = default;
    virtual void rect(int x, int y, int w, int h, uint32_t color) = 0;
    virtual void text(const std::string& text, int x, int y, uint32_t color, int scale = 1) = 0;
};
void drawGame(const Games& games, GamePainter& painter);
}
