#include "playlists.h"
#include "order.h"
#include <cassert>
#include <iostream>
#include <map>
#include <set>

class MemoryFiles : public ct::PlaylistFiles {
public:
    std::map<std::string, std::string> data;
    bool mounted = true, failWrite = false, failFinalize = false, failRestore = false, corruptWrite = false;
    bool ready() const override { return mounted; }
    bool exists(const std::string& path) override { return data.count(path); }
    bool read(const std::string& path, std::string& out, size_t maximum) override {
        auto it = data.find(path);
        if (it == data.end() || it->second.size() > maximum) return false;
        out = it->second; return true;
    }
    bool write(const std::string& path, const std::string& value) override {
        data[path] = corruptWrite ? "partial" : value;
        return !failWrite;
    }
    bool rename(const std::string& from, const std::string& to) override {
        if ((failFinalize && from.find(".tmp") != std::string::npos) ||
            (failRestore && from.find(".bak") != std::string::npos) || !exists(from) || exists(to)) return false;
        data[to] = std::move(data[from]); data.erase(from); return true;
    }
    bool remove(const std::string& path) override { return data.erase(path); }
};
int main() {
    MemoryFiles files;
    ct::Playlists lists(files);
    assert(lists.begin()); assert(lists.list().empty());
    uint32_t road = 0, quiet = 0;
    assert(lists.create("Road trip", road));
    assert(lists.create("Quiet", quiet)); assert(road != quiet);
    uint32_t ignored = 0;
    assert(!lists.create("road TRIP", ignored));
    for (const auto& name : {"", " padded", "trailing ", "bad\nname"}) assert(!lists.create(name, ignored));
    assert(!lists.create(std::string(64, 'x'), ignored));
    ct::Playlist playlist;
    assert(lists.load(road, playlist));
    playlist.paths = {"/Music/Artist/Track 2.mp3", "/Music/Artist/Track 1.mp3", "/@demo.mp3"};
    assert(lists.save(playlist));
    ct::Playlists reboot(files);
    assert(reboot.begin()); assert(reboot.list().size() == 2);
    ct::Playlist restored;
    assert(reboot.load(road, restored));
    assert(restored.paths == playlist.paths); assert(restored.name == "Road trip");
    restored.name = "Road trip \"2026\"";
    assert(reboot.save(restored)); assert(reboot.list()[0].name == restored.name);
    auto good = restored;
    restored.paths.push_back(restored.paths[0]); assert(!reboot.save(restored));
    restored = good; restored.paths[0] = "/Music/../secret.mp3"; assert(!reboot.save(restored));
    restored = good; restored.paths[0] = "/Music/not-music.wav"; assert(!reboot.save(restored));
    restored = good;
    for (uint32_t i = 0; i < ct::Playlists::MaxTracks; ++i) restored.paths.push_back("/Music/" + std::to_string(i) + ".mp3");
    assert(!reboot.save(restored));
    for (const std::string& data : {"{}", "{", "[]", "{\"version\":2,\"name\":\"x\",\"paths\":[]}",
        "{\"version\":1,\"name\":\"x\",\"paths\":[null]}",
        "{\"version\":1,\"name\":\"x\",\"paths\":[\"/a.mp3\",\"/a.mp3\"]}"}) assert(!ct::Playlists::decode(data, restored));
    assert(!ct::Playlists::decode(ct::Playlists::encode(good) + " trailing", restored));
    assert(!ct::Playlists::decode(std::string(40000, 'x'), restored));
    assert(ct::Playlists::decode(ct::Playlists::encode(good), restored)); assert(restored.paths == good.paths);
    for (int failure = 0; failure < 4; ++failure) {
        files.failWrite = failure == 0; files.corruptWrite = failure == 1;
        files.failFinalize = failure >= 2; files.failRestore = failure == 3;
        auto edit = good; edit.name = "Should not replace good version";
        assert(!reboot.save(edit));
        files.failWrite = files.corruptWrite = files.failFinalize = files.failRestore = false;
        ct::Playlists afterCrash(files); assert(afterCrash.begin());
        assert(afterCrash.load(road, restored)); assert(restored.name == good.name); assert(restored.paths == good.paths);
    }
    const auto file = "/.cardtunes/playlist-" + std::to_string(road) + ".json";
    files.data[file + ".bak"] = ct::Playlists::encode(good); files.data[file] = "corrupt";
    assert(reboot.load(road, restored)); assert(restored.name == good.name);
    files.mounted = false; assert(!reboot.create("Offline", ignored)); assert(!reboot.erase(road));
    files.mounted = true;
    assert(reboot.erase(road)); assert(!reboot.load(road, restored));
    ct::Playlists afterDelete(files); assert(afterDelete.begin()); assert(afterDelete.list().size() == 1);
    for (uint32_t i = 1; i < ct::Playlists::MaxLists; ++i) assert(afterDelete.create("List " + std::to_string(i), ignored));
    assert(!afterDelete.create("One too many", ignored));
    ct::Playlist maximum{quiet, "Maximum", {}};
    for (uint32_t i = 0; i < ct::Playlists::MaxTracks; ++i) maximum.paths.push_back("/Music/" + std::string(160, 'x') + std::to_string(i) + ".mp3");
    assert(afterDelete.save(maximum)); assert(afterDelete.load(quiet, restored)); assert(restored.paths == maximum.paths);

    ct::Order order;
    const std::vector<uint32_t> ids{9, 3, 42, 1};
    order.reset(ids); order.repeat(ct::Repeat::Off);
    assert(order.first() == 9); assert(order.next() == 9); assert(order.next() == 3);
    assert(order.next() == 42); assert(order.next() == 1); assert(order.next(true) == -1);
    assert(!order.enqueue(0)); assert(order.select(0) == -1);
    order.repeat(ct::Repeat::All); assert(order.next(true) == 9);
    order.select(3); order.repeat(ct::Repeat::One); assert(order.next(true) == 3);
    assert(order.enqueue(42)); assert(order.next() == 42); assert(order.previous() == 3);
    order.reset(ids); order.select(42); order.seed(23); order.shuffle(true);
    std::set<int> seen{42};
    for (int i = 0; i < 3; ++i) assert(seen.insert(order.next()).second);
    assert((seen == std::set<int>{1, 3, 9, 42}));
    order.shuffle(false); order.select(9); assert(order.next() == 3);
    order.grow(100); assert(order.size() == 4); assert(!order.contains(99));
    order.select(3); assert(order.enqueue(1)); order.replace({42, 3, 9});
    assert(order.current() == 3); assert(order.queue().empty()); assert(order.next() == 9);
    order.replace({42}); assert(order.current() == -1); assert(order.next() == 42);
    order.reset(std::vector<uint32_t>{}); assert(order.next() == -1); assert(order.previous() == -1);
    order.reset(2); order.grow(5); assert(order.size() == 5); assert(order.select(4) == 4);
    std::cout << "Playlist persistence, validation, power-loss recovery, limits, and scoped playback passed\n";
}
