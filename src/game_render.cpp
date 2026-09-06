#include "games.h"
#include <algorithm>
#include <cmath>

namespace ct {
namespace {
constexpr uint32_t Bg = 0x101513, White = 0xf2f4f1, Muted = 0xa3aea5, Green = 0xd2f36b;
constexpr uint32_t Colors[] = {0x242c29, 0xeec94f, 0xbe85ef, 0xf49b55, 0x629fee, 0x5dd6da, 0x8acc7b, 0xf07895};
void stat(GamePainter& p, const char* label, uint32_t value, int x, int y) {
    p.text(label, x, y, Muted); p.text(std::to_string(value), x, y + 11, White);
}
}
void drawGame(const Games& games, GamePainter& p) {
    p.rect(0, 0, 240, 135, Bg);
    auto id = games.active();
    if (id == GameId::Blocks) {
        p.rect(8, 6, 64, 124, 0x536058);
        p.rect(10, 8, 60, 120, 0x1c2420);
        auto cell = [&](int x, int y, int color) {
            if (x >= 0 && x < 10 && y >= 0 && y < 20) p.rect(10 + x * 6, 8 + y * 6, 5, 5, Colors[std::clamp(color, 0, 7)]);
        };
        const auto& f = games.falling;
        for (int y = 0; y < 20; ++y) for (int x = 0; x < 10; ++x) if (f.grid[y][x]) cell(x, y, f.grid[y][x]);
        if (!f.game_over) for (int i = 0; i < 4; ++i) cell(f.current_x + f.current_shape[i * 2], f.current_y + f.current_shape[i * 2 + 1], f.current_shape_kind + 1);
        p.text("FALLING BLOCKS", 86, 9, Green);
        stat(p, "SCORE", f.score, 86, 30); stat(p, "BEST", games.best(id), 164, 30);
        stat(p, "LEVEL", f.level + 1, 86, 63); stat(p, "LINES", f.lines_cleared, 164, 63);
        p.text("NEXT", 86, 100, Muted);
        // Preview uses the same canonical shape definitions as the rules engine.
        int shape[8]; blocks::game_shape(f.next_shape_kind, shape);
        for (int i = 0; i < 4; ++i) p.rect(144 + shape[i * 2] * 7, 107 + shape[i * 2 + 1] * 7, 6, 6, Colors[f.next_shape_kind + 1]);
    } else if (id == GameId::Breakout) {
        const auto& b = games.breakout;
        p.text("BREAKOUT", 7, 5, Green);
        p.text(std::to_string(b.score), 87, 5, White);
        p.text("LIVES " + std::to_string(b.lives), 184, 5, Muted);
        p.rect(0, 18, 240, 2, 0x536058); p.rect(0, 20, 3, 115, 0x536058); p.rect(237, 20, 3, 115, 0x536058);
        for (int i = 0; i < 40; ++i) if (b.bricks[i]) p.rect(9 + i % 8 * 28, 29 + i / 8 * 9, 26, 6, Colors[1 + i / 8]);
        p.rect(std::lround(b.paddle) - 18, 123, 36, 4, Green);
        if (b.y < 135) p.rect(std::lround(b.x) - 2, std::min(131L, std::lround(b.y) - 2), 4, 4, White);
        if (b.serving && !b.over) p.text("READY", 105, 90, White);
    } else if (id == GameId::Merge2048) {
        const auto& m = games.merge;
        for (int x = 0; x < 4; ++x) for (int y = 0; y < 4; ++y) {
            auto tile = m.board[x][y]; int px = 4 + x * 34, py = 9 + y * 30;
            p.rect(px, py, 32, 28, tile ? Colors[1 + (tile - 1) % 7] : Colors[0]);
            if (tile) { auto number = std::to_string(1u << tile); p.text(number, px + (32 - int(number.size()) * 6) / 2, py + 10, Bg); }
        }
        p.text("2048", 154, 10, Green, 2);
        stat(p, "SCORE", m.score, 154, 44); stat(p, "BEST", games.best(id), 154, 81);
    }
    if (id != GameId::None && (games.paused() || games.over())) {
        p.rect(66, 29, 152, 94, 0x536058); p.rect(68, 31, 148, 90, Bg);
        p.text(games.won() ? "YOU WON" : games.over() ? "GAME OVER" : "PAUSED", 80, 40, Green);
        const char* names[] = {"Resume", "New game", "Exit to music"};
        for (int i = 0; i < 3; ++i) {
            bool selected = i == games.menuSelection();
            if (selected) p.rect(74, 58 + i * 19, 136, 17, Green);
            p.text(names[i], 80, 63 + i * 19, selected ? Bg : games.over() && !i ? 0x536058 : White);
        }
    }
}
}
