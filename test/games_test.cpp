#include "games.h"
#include <cassert>
#include <cmath>
#include <cstring>
#include <iostream>
#include <map>
#include <random>
#include <cJSON.h>
using namespace ct;
static int allocations = 0, failAllocation = -1;
static void* faultMalloc(size_t n) { if (failAllocation == 0) return nullptr; if (failAllocation > 0) --failAllocation; auto p = std::malloc(n); if (p) ++allocations; return p; }
static void faultFree(void* p) { if (p) --allocations; std::free(p); }

class Files : public PlaylistFiles {
public:
    std::map<std::string, std::string> data;
    bool mounted = true, failWrite = false, failRename = false, corrupt = false;
    std::string failRemovePath, failRenameFrom;
    bool failRead = false;
    bool ready() const override { return mounted; }
    bool exists(const std::string& p) override { return data.count(p); }
    bool read(const std::string& p, std::string& out, size_t max) override { if (failRead || !exists(p) || data[p].size() > max) return false; out = data[p]; return true; }
    bool write(const std::string& p, const std::string& in) override { data[p] = corrupt ? "partial" : in; return !failWrite; }
    bool rename(const std::string& from, const std::string& to) override { if (failRename || from == failRenameFrom || !exists(from) || exists(to)) return false; data[to] = data[from]; data.erase(from); return true; }
    bool remove(const std::string& p) override { return p != failRemovePath && data.erase(p); }
};
class Bounds : public GamePainter {
public:
    unsigned rectangles = 0, labels = 0;
    void rect(int x, int y, int w, int h, uint32_t) override { assert(x >= 0 && y >= 0 && w > 0 && h > 0 && x + w <= 240 && y + h <= 135); ++rectangles; }
    void text(const std::string& s, int x, int y, uint32_t, int scale) override { assert(x >= 0 && y >= 0 && x + int(s.size()) * 6 * scale <= 240 && y + 8 * scale <= 135); ++labels; }
};
static void roundtrip(const Games& g) {
    auto data = g.serialize(); Games restored;
    assert(restored.restore(data)); assert(restored.serialize() == data);
    assert(restored.active() == GameId::None);
}
int main() {
    std::srand(42);
    Games g;
    assert(!g.start(GameId::None));
    assert(gameId("bad") == GameId::None && gameKey("toggle") == GameKey::None);
    assert(g.start(GameId::Blocks));
    auto y = g.falling.current_y;
    g.tick(1000000); assert(g.falling.current_y == y); // bounded catch-up
    for (int i = 0; i < 20; ++i) g.input(GameKey::Left);
    for (int i = 0; i < 4; ++i) assert(g.falling.current_x + g.falling.current_shape[i * 2] >= 0);
    g.pause(); auto saved = g.serialize(); g.tick(10000); assert(g.serialize() == saved);
    auto revision = g.revision(); g.tick(1000); assert(g.revision() == revision);
    g.input(GameKey::Down); g.input(GameKey::Primary); assert(!g.paused() && !g.over());
    // A vertical I piece completes four rows in one drop.
    auto& f = g.falling;
    std::memset(f.grid, 0, sizeof(f.grid)); f.current_shape_kind = 4; blocks::game_shape(4, f.current_shape);
    f.current_x = 5; f.current_y = 0;
    for (int row = 16; row < 20; ++row) for (int x = 0; x < 10; ++x) f.grid[row][x] = x == 5 ? 0 : 1;
    g.input(GameKey::Primary); assert(f.lines_cleared == 4 && f.score == 701);
    for (const auto& row : f.grid) for (auto cell : row) assert(cell == 0);
    roundtrip(g);
    g.leave(); g.start(GameId::Blocks); assert(g.falling.lines_cleared == 4);
    assert(!g.input(GameKey::Exit)); g.leave();

    g.start(GameId::Merge2048); g.merge = MergeState{};
    for (int x = 0; x < 4; ++x) g.merge.board[x][0] = 1;
    g.input(GameKey::Left); assert(g.merge.board[0][0] == 2 && g.merge.board[1][0] == 2 && g.merge.score == 8);
    g.merge = MergeState{}; g.merge.board[0][0] = 10; g.merge.board[1][0] = 10;
    g.input(GameKey::Left); assert(g.won() && g.over() && g.paused() && g.merge.score == 2048);
    roundtrip(g); auto best = g.best(GameId::Merge2048);
    g.restart(); assert(g.best(GameId::Merge2048) == best && !g.over());
    g.merge = MergeState{}; g.merge.board[0][0] = 1;
    auto noMove = g.serialize(); g.input(GameKey::Left); assert(g.serialize() == noMove);
    revision = g.revision(); for (int i = 0; i < 100; ++i) g.tick(33); assert(g.revision() == revision);

    g.leave(); g.start(GameId::Breakout);
    g.held(true, false); for (int i = 0; i < 100; ++i) g.tick(50);
    assert(g.breakout.paddle == 22 && g.breakout.x == 22);
    g.held(false, false); g.input(GameKey::Primary); assert(!g.breakout.serving);
    auto& b = g.breakout;
    b.x = 5.5; b.y = 90; b.vx = -200; b.vy = 0; g.tick(8); assert(b.vx > 0 && b.x >= 5);
    b.x = b.paddle; b.y = 120; b.vx = 0; b.vy = 200; g.tick(8); assert(b.vy < 0);
    b.x = 22; b.y = 26; b.vx = 0; b.vy = 200; g.tick(8); assert(b.bricks[0] == 0 && b.score == 10 && b.vy < 0);
    for (int life = 3; life > 0; --life) { b.serving = false; b.x = 120; b.y = 138; b.vy = 200; g.tick(8); assert(b.lives == unsigned(life - 1)); }
    assert(g.over() && g.paused()); g.tick(10000); assert(b.lives == 0); roundtrip(g);
    g.restart(); assert(b.lives == 3 && b.score == 0 && g.best(GameId::Breakout) == 10);
    b.bricks.fill(0); b.bricks[0] = 1; b.serving = false; b.x = 22; b.y = 26; b.vx = 0; b.vy = 200;
    g.tick(8); assert(b.level == 2 && b.serving); for (auto brick : b.bricks) assert(brick);

    // Exercise the real engines and renderer with repeatable, adversarial inputs.
    std::mt19937 rng(13);
    for (auto id : {GameId::Blocks, GameId::Breakout, GameId::Merge2048}) {
        g.leave(); g.start(id); g.restart();
        for (int i = 0; i < 20000; ++i) {
            if (g.over()) g.restart();
            g.input(GameKey(1 + rng() % 5)); g.tick(rng() % 80);
            Bounds bounds; drawGame(g, bounds); assert(bounds.rectangles > 4 && bounds.labels > 0);
            if (i % 100 == 0) roundtrip(g);
        }
        g.pause(); Bounds bounds; drawGame(g, bounds); roundtrip(g);
    }
    g.leave(); saved = g.serialize();
    for (const std::string& bad : {"", "{}", "[]", "{\"version\":2}", "{\"version\":1,\"state\":[1]}", "{\"version\":1,\"state\":[[[]]]}"}) { assert(!g.restore(bad)); assert(g.serialize() == saved); }
    assert(!g.restore(saved + " trailing")); assert(!g.restore(std::string(8193, 'x')));
    const std::string deep = "{\"version\":1,\"state\":[0,0,0,0,0,0],\"pad\":\"" + std::string(800, ']') + "\",\"extra\":" + std::string(800, '[') + "0" + std::string(800, ']') + "}";
    assert(!g.restore(deep)); assert(g.serialize() == saved);
    Games malformed, check;
    malformed.start(GameId::Blocks);
    const int shifted[] = {-2,0,-1,0,-2,1,-1,1};
    std::copy(shifted, shifted + 8, malformed.falling.current_shape);
    malformed.falling.current_shape_kind = 0; malformed.falling.current_x = 9;
    assert(!check.restore(malformed.serialize()));
    malformed.leave(); malformed.start(GameId::Merge2048); malformed.restart(); malformed.merge.over = true;
    // Isolate the 2048 validation from the malformed Blocks state.
    malformed.leave(); malformed.start(GameId::Blocks); malformed.restart(); malformed.leave();
    assert(!check.restore(malformed.serialize()));
    malformed.merge.over = false;
    for (int x = 0; x < 4; ++x) for (int y = 0; y < 4; ++y) malformed.merge.board[x][y] = 1 + (x + y) % 2;
    assert(!check.restore(malformed.serialize()));
    malformed.merge.over = true; assert(check.restore(malformed.serialize()));
    Files files; GameStore store(files); assert(store.load(g));
    g.start(GameId::Blocks); g.restart(); assert(store.save(g)); auto first = g.serialize();
    g.input(GameKey::Primary); assert(store.save(g));
    files.data["/.cardtunes/games-v1.json"] = deep;
    Games recovered; assert(store.load(recovered)); assert(recovered.serialize() == first);
    for (int failure = 0; failure < 3; ++failure) {
        recovered.start(GameId::Blocks); recovered.input(GameKey::Primary);
        files.failWrite = failure == 0; files.corrupt = failure == 1; files.failRename = failure == 2;
        assert(!store.save(recovered) && recovered.dirty());
        files.failWrite = files.corrupt = files.failRename = false;
        Games reboot; assert(store.load(reboot)); assert(reboot.serialize() == first);
    }
    assert(store.save(recovered)); assert(!recovered.dirty());
    for (int failure = 0; failure < 3; ++failure) {
        Files rotation; GameStore rotate(rotation); Games current;
        assert(rotate.load(current));
        current.start(GameId::Blocks); assert(rotate.save(current));
        current.input(GameKey::Primary); assert(rotate.save(current));
        const auto good = current.serialize(); current.input(GameKey::Primary);
        rotation.failRemovePath = failure == 0 ? "/.cardtunes/games-v1.bak" : "";
        rotation.failRenameFrom = failure == 1 ? "/.cardtunes/games-v1.json" : failure == 2 ? "/.cardtunes/games-v1.tmp" : "";
        assert(!rotate.save(current) && current.dirty());
        Games reboot; assert(rotate.load(reboot)); assert(reboot.serialize() == good);
    }
    Games ram, card;
    ram.start(GameId::Merge2048); ram.leave();
    card.start(GameId::Blocks); card.input(GameKey::Primary); card.leave();
    ram.mergeSaved(card); assert(ram.hasSave(GameId::Merge2048)); assert(ram.score(GameId::Blocks) == card.score(GameId::Blocks)); roundtrip(ram);
    files.failRead = true; GameStore transient(files); Games unloaded;
    assert(!transient.load(unloaded) && !transient.restored()); auto cardData = files.data;
    unloaded.start(GameId::Blocks); assert(!transient.save(unloaded)); assert(files.data == cardData);
    files.failRead = false; assert(transient.load(card)); unloaded.mergeSaved(card); assert(transient.save(unloaded));
    cJSON_Hooks hooks{faultMalloc, faultFree}; cJSON_InitHooks(&hooks);
    for (int fail = 0; fail < 400; ++fail) {
        failAllocation = fail; (void)g.serialize(); assert(allocations == 0);
    }
    failAllocation = -1; cJSON_InitHooks(nullptr);
    files.mounted = false; recovered.input(GameKey::Primary); assert(!store.save(recovered)); assert(!store.error().empty());
    std::cout << "Games: controls, 4-line clear, 2048 merges/win, collisions/lives/waves, 60000 fuzz steps, render bounds and save recovery passed\n";
}
