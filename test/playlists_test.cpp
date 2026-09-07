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
    size_t maxRead = 0, wholeReads = 0;
    class Input : public ct::Reader {
    public:
        Input(MemoryFiles& files, const std::string& path) : files_(files), path_(path) {}
        size_t read(void* data, size_t n) override {
            files_.maxRead = std::max(files_.maxRead, n);
            auto& value = files_.data.at(path_);
            n = std::min(n, value.size() - offset_);
            memcpy(data, value.data() + offset_, n); offset_ += n; return n;
        }
        bool seek(uint32_t at) override { if (at > size()) return false; offset_ = at; return true; }
        uint32_t size() const override { return files_.data.at(path_).size(); }
    private:
        MemoryFiles& files_; std::string path_; size_t offset_ = 0;
    };
    class Output : public ct::PlaylistWriter {
    public:
        Output(MemoryFiles& files, const std::string& path) : files_(files), path_(path) { files_.data[path].clear(); }
        bool write(const std::string& data) override {
            files_.data[path_] += files_.corruptWrite ? "partial" : data;
            return !files_.failWrite;
        }
        bool finish() override { return true; }
    private:
        MemoryFiles& files_; std::string path_;
    };
    std::unique_ptr<ct::Reader> openReader(const std::string& path) override {
        if (!exists(path)) return nullptr;
        return std::make_unique<Input>(*this, path);
    }
    std::unique_ptr<ct::PlaylistWriter> openWriter(const std::string& path) override { return std::make_unique<Output>(*this, path); }
    bool ready() const override { return mounted; }
    bool exists(const std::string& path) override { return data.count(path); }
    bool read(const std::string& path, std::string& out, size_t maximum) override {
        ++wholeReads;
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
static std::vector<std::string> values(const ct::PlaylistPaths& paths) {
    std::vector<std::string> result;
    assert(paths.each([&](size_t, const std::string& path) { result.push_back(path); return true; }));
    return result;
}
int main() {
    static_assert(ct::Playlists::MaxTracks == 1000);
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
    for (const auto& path : {"/Music/Artist/Track 2.mp3", "/Music/Artist/Track 1.mp3", "/@demo.mp3"}) playlist.paths.push_back(path);
    assert(lists.save(playlist));
    ct::Playlists reboot(files);
    assert(reboot.begin()); assert(reboot.list().size() == 2);
    ct::Playlist restored;
    assert(reboot.load(road, restored));
    assert(values(restored.paths) == values(playlist.paths)); assert(restored.name == "Road trip");
    restored.name = "Road trip \"2026\"";
    assert(reboot.save(restored)); assert(reboot.list()[0].name == restored.name);
    assert(reboot.load(road, restored));
    auto good = restored;
    restored.paths.push_back(values(restored.paths)[0]); assert(!reboot.save(restored));
    restored = good; restored.paths.push_back("/Music/../secret.mp3"); assert(!reboot.save(restored));
    restored = good; restored.paths.push_back("/Music/not-music.wav"); assert(!reboot.save(restored));
    restored = good;
    for (uint32_t i = 0; i < ct::Playlists::MaxTracks; ++i) restored.paths.push_back("/Music/" + std::to_string(i) + ".mp3");
    assert(!reboot.save(restored));
    for (const std::string& data : {"{}", "{", "[]", "{\"version\":2,\"name\":\"x\",\"paths\":[]}",
        "{\"version\":1,\"name\":\"x\",\"paths\":[null]}",
        "{\"version\":1,\"name\":\"x\",\"paths\":[\"/a.mp3\",\"/a.mp3\"]}"}) assert(!ct::Playlists::decode(data, restored));
    assert(!ct::Playlists::decode(ct::Playlists::encode(good) + " trailing", restored));
    assert(!ct::Playlists::decode(std::string(40000, 'x'), restored));
    assert(ct::Playlists::decode(ct::Playlists::encode(good), restored)); assert(values(restored.paths) == values(good.paths));
    for (int failure = 0; failure < 4; ++failure) {
        files.failWrite = failure == 0; files.corruptWrite = failure == 1;
        files.failFinalize = failure >= 2; files.failRestore = failure == 3;
        auto edit = good; edit.name = "Should not replace good version";
        assert(!reboot.save(edit));
        files.failWrite = files.corruptWrite = files.failFinalize = files.failRestore = false;
        ct::Playlists afterCrash(files); assert(afterCrash.begin());
        assert(afterCrash.load(road, restored)); assert(restored.name == good.name); assert(values(restored.paths) == values(good.paths));
    }
    const auto file = "/.cardtunes/playlist-" + std::to_string(road) + ".jsonl";
    files.data[file + ".bak"] = files.data[file]; files.data[file] = "corrupt";
    assert(reboot.load(road, restored)); assert(restored.name == good.name);
    files.mounted = false; assert(!reboot.create("Offline", ignored)); assert(!reboot.erase(road));
    files.mounted = true;
    assert(reboot.erase(road)); assert(!reboot.load(road, restored));
    ct::Playlists afterDelete(files); assert(afterDelete.begin()); assert(afterDelete.list().size() == 1);
    for (uint32_t i = 1; i < ct::Playlists::MaxLists; ++i) assert(afterDelete.create("List " + std::to_string(i), ignored));
    assert(!afterDelete.create("One too many", ignored));
    ct::Playlist maximum{quiet, "Maximum", {}};
    for (uint32_t i = 0; i < ct::Playlists::MaxTracks; ++i) maximum.paths.push_back("/Music/" + std::string(160, 'x') + std::to_string(i) + ".mp3");
    assert(afterDelete.save(maximum));
    auto reads = files.wholeReads;
    assert(afterDelete.load(quiet, restored)); assert(values(restored.paths) == values(maximum.paths));
    assert(files.wholeReads == reads && files.maxRead <= 512);
    std::string last;
    assert(restored.paths.read(999, last) && last == values(maximum.paths).back());
    restored.paths.move(999, 0);
    assert(afterDelete.save(restored)); assert(afterDelete.load(quiet, restored));
    assert(restored.paths.read(0, last) && last == values(maximum.paths).back());
    restored.paths.erase(500); restored.paths.push_back("/Music/Replacement.mp3");
    assert(afterDelete.save(restored)); assert(afterDelete.load(quiet, restored));
    assert(restored.paths.read(999, last) && last == "/Music/Replacement.mp3");
    restored.paths.push_back("/Music/Too many.mp3"); assert(!afterDelete.save(restored));
    ct::Playlists largeReboot(files); assert(largeReboot.begin());
    assert(largeReboot.load(quiet, restored)); assert(restored.paths.size() == 1000);

    MemoryFiles legacyFiles;
    auto legacyPath = "/.cardtunes/playlist-1.json";
    ct::Playlist old{1, "Existing playlist", {}}; old.paths.push_back("/Music/Old.mp3");
    legacyFiles.data[legacyPath + std::string(".bak")] = ct::Playlists::encode(old);
    ct::Playlists migration(legacyFiles); assert(migration.begin());
    assert(migration.load(1, restored)); assert(values(restored.paths) == values(old.paths));
    restored.paths.push_back("/Music/New.mp3"); assert(migration.save(restored));
    assert(!legacyFiles.exists(legacyPath)); assert(legacyFiles.exists("/.cardtunes/playlist-1.jsonl"));
    assert(migration.load(1, restored)); assert(restored.paths.size() == 2);
    const auto migrated = legacyFiles.data["/.cardtunes/playlist-1.jsonl"];
    for (const auto& broken : {migrated.substr(0, migrated.size() - 1), migrated + "extra\n",
        std::string("{\"version\":2,\"name\":\"x\",\"count\":1001}\n"),
        std::string("{\"version\":2,\"name\":\"x\",\"count\":2}\n\"/a.mp3\"\n\"/a.mp3\"\n")}) {
        legacyFiles.data["/.cardtunes/playlist-1.jsonl"] = broken;
        assert(!migration.load(1, restored));
    }
    legacyFiles.data["/.cardtunes/playlist-1.jsonl"] = migrated;
    assert(migration.load(1, restored)); assert(migration.erase(1));

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
