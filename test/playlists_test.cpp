#include "playlists.h"
#include "order.h"
#include <cJSON.h>
#include <cassert>
#include <cstdlib>
#include <cstring>
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
static size_t jsonAllocations = 0;
static void* countJsonAllocation(size_t size) { ++jsonAllocations; return std::malloc(size); }
static void testJsonDepth() {
    MemoryFiles files;
    ct::Playlists lists(files);
    ct::Playlist playlist;
    const std::string file = "/.cardtunes/playlist-1.jsonl";
    cJSON_Hooks hooks{countJsonAllocation, std::free};
    cJSON_InitHooks(&hooks);
    for (int version : {2, 3}) {
        const std::string header = "{\"version\":" + std::to_string(version) + ",\"name\":\"Depth\",\"count\":1}";
        for (unsigned depth : {1u, 200u}) {
            files.data[file] = header.substr(0, header.size() - 1) + ",\"extra\":" +
                std::string(depth, '[') + "0" + std::string(depth, ']') + "}\n\"/Music/A.mp3\"\n";
            assert(files.data[file].find('\n') < 512);
            jsonAllocations = 0;
            assert(!lists.load(1, playlist)); assert(jsonAllocations == 0);
        }
        jsonAllocations = 0;
        auto json = cJSON_ParseWithOpts(header.c_str(), nullptr, true); assert(json);
        cJSON_Delete(json);
        const auto headerAllocations = jsonAllocations;
        for (unsigned depth : {1u, 200u}) {
            files.data[file] = header + "\n" + std::string(depth, '[') +
                "\"/Music/A.mp3\"" + std::string(depth, ']') + "\n";
            jsonAllocations = 0;
            assert(!lists.load(1, playlist)); assert(jsonAllocations == headerAllocations);
        }
        files.data[file] = "{\"version\":" + std::to_string(version) +
            R"(,"name":"[\"Live\"] \\","count":1})" "\n" +
            R"("\/Music\/[{\"Live\"}]\u0020mix.mp3")" "\n";
        assert(lists.load(1, playlist));
        assert(playlist.name == "[\"Live\"] \\");
        assert(values(playlist.paths) == std::vector<std::string>{"/Music/[{\"Live\"}] mix.mp3"});
    }
    const std::string legacy = R"({"version":1,"name":"Depth","paths":[],"extra":)";
    for (unsigned depth : {2u, 200u}) {
        jsonAllocations = 0;
        assert(!ct::Playlists::decode(legacy + std::string(depth, '[') + "0" + std::string(depth, ']') + "}", playlist));
        assert(jsonAllocations == 0);
    }
    assert(ct::Playlists::decode(R"({"version":1,"name":"[\"Live\"] \\","paths":["\/Music\/[{\"Live\"}]\u0020mix.mp3"]})", playlist));
    assert(playlist.name == "[\"Live\"] \\");
    assert(values(playlist.paths) == std::vector<std::string>{"/Music/[{\"Live\"}] mix.mp3"});
    cJSON_InitHooks(nullptr);
    std::cout << "Playlist JSON depth guards reject before parsing; escaped strings passed\n";
}
static void testLegacyJsonNodes() {
    ct::Playlist original{7, "[\"Legacy, : {}\"] \\", {}};
    for (unsigned i = 0; i < 128; ++i)
        original.paths.push_back("/Music/[{\"Live\"}],:/" + std::string(150, 'x') + std::to_string(i) + ".mp3");
    const auto encoded = ct::Playlists::encode(original);
    assert(encoded.size() > 20000 && encoded.size() <= 32768);
    ct::Playlist playlist;
    cJSON_Hooks hooks{countJsonAllocation, std::free};
    cJSON_InitHooks(&hooks);
    assert(ct::Playlists::decode(encoded, playlist));
    assert(playlist.name == original.name && values(playlist.paths) == values(original.paths));

    // Root + version + name + paths + 128 strings = 132 nodes; keys are not nodes.
    std::string boundary = encoded.substr(0, encoded.size() - 1);
    for (const char* member : {R"(,"a":null)", R"(,"b":true)", R"(,"c":false)", R"(,"d":-1.5e2)",
         R"(,"e":{})", ",\"f\":[ \t\r\n ]", R"(,"g":{"x":0})"}) boundary += member;
    assert(ct::Playlists::decode(boundary + "}", playlist));
    assert(values(playlist.paths) == values(original.paths));
    auto rejectedBeforeParse = [&](const std::string& data) {
        assert(data.size() <= 32768);
        jsonAllocations = 0;
        assert(!ct::Playlists::decode(data, playlist));
        assert(jsonAllocations == 0);
        assert(playlist.name == original.name && values(playlist.paths) == values(original.paths));
    };
    rejectedBeforeParse(boundary + R"(,"overflow":0})");
    // A malformed trailing member also makes cJSON allocate a node before failing.
    rejectedBeforeParse(boundary + ",}");
    for (const char* item : {"0", "{}"}) {
        std::string wide = "[";
        for (unsigned i = 0; i < 10000; ++i) { if (i) wide += ','; wide += item; }
        wide += ']';
        rejectedBeforeParse(wide);
        if (std::string(item) == "0")
            rejectedBeforeParse(R"({"version":1,"name":"Wide","paths":)" + wide + "}");
    }
    std::string members = R"({"version":1,"name":"Wide","paths":[])";
    for (unsigned i = 0; i < 137; ++i) members += R"(,"extra":{})";
    rejectedBeforeParse(members + "}");

    assert(ct::Playlists::decode(R"({"version":1,"name":"[\"Live, : {}\"] \\","paths":["\/Music\/[{\"Live, :\"}]\u0020mix.mp3"]})", playlist));
    assert(playlist.name == "[\"Live, : {}\"] \\");
    assert(values(playlist.paths) == std::vector<std::string>{"/Music/[{\"Live, :\"}] mix.mp3"});
    cJSON_InitHooks(nullptr);
    std::cout << "Legacy JSON node budget: 140 accepted, 141 and 10000-element inputs rejected before parsing; 128 long escaped paths passed\n";
}
int main() {
    testJsonDepth();
    testLegacyJsonNodes();
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

    MemoryFiles imports;
    ct::Playlists imported(imports); assert(imported.begin());
    const std::string source = "/.cardtunes/playlist-import.tmp";
    const std::string ordered = "{\"version\":3,\"name\":\"Folder order\",\"count\":4}\n\"/Music/B.mp3\"\n\"/Music/A.mp3\"\n\"/Music/B.mp3\"\n\"/Music/Missing.mp3\"\n";
    imports.data[source] = ordered;
    uint32_t importedId = 0;
    assert(imported.importFile(source, importedId)); assert(importedId == 1);
    imports.remove(source);
    ct::Playlists importedReboot(imports); assert(importedReboot.begin());
    assert(importedReboot.load(importedId, restored)); assert(restored.allowRepeats);
    assert((values(restored.paths) == std::vector<std::string>{"/Music/B.mp3", "/Music/A.mp3", "/Music/B.mp3", "/Music/Missing.mp3"}));
    imports.data[source] = ordered;
    uint32_t duplicateName = 0; assert(!importedReboot.importFile(source, duplicateName)); assert(duplicateName == 0);
    imports.failFinalize = true; assert(!importedReboot.importFile(source, importedId)); imports.failFinalize = false;
    assert(importedReboot.load(importedId, restored)); assert(restored.paths.size() == 4);
    for (auto broken : {ordered.substr(0, ordered.size() - 1), ordered + "extra\n",
         std::string("{\"version\":3,\"name\":\"x\",\"count\":1001}\n"),
         std::string("{\"version\":3,\"name\":\"x\",\"count\":1}\n\"/Music/../bad.mp3\"\n")}) {
        imports.data[source] = broken; assert(!importedReboot.importFile(source, importedId));
        assert(importedReboot.load(importedId, restored)); assert(restored.paths.size() == 4);
    }
    imports.data[source] = ordered; assert(importedReboot.importFile(source, importedId));
    assert(imports.maxRead <= 512 && imports.wholeReads == 0);

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
    const std::vector<uint32_t> repeated{9, 3, 9, UINT32_MAX, 1, 9};
    order.reset(repeated); order.repeat(ct::Repeat::Off);
    assert(order.size() == 5); assert(!order.contains(UINT32_MAX)); assert(order.selectPosition(3) == -1);
    for (int position : {0, 1, 2, 4, 5}) {
        assert(order.next() == int(repeated[position])); assert(order.currentPosition() == position);
    }
    assert(order.next() == -1); assert(order.previous() == 1); assert(order.currentPosition() == 4);
    assert(order.previous() == 9); assert(order.currentPosition() == 2);
    order.replace(repeated); assert(order.currentPosition() == 2); assert(order.next() == 1);
    order.selectPosition(2); order.repeat(ct::Repeat::One); assert(order.next(true) == 9); assert(order.currentPosition() == 2);
    order.shuffle(true); order.repeat(ct::Repeat::Off);
    std::set<int> positions{2};
    for (int i = 0; i < 4; ++i) { assert(order.next() >= 0); assert(positions.insert(order.currentPosition()).second); }
    assert((positions == std::set<int>{0, 1, 2, 4, 5})); assert(order.next() == -1);
    order.shuffle(false); order.selectPosition(2); assert(order.next() == 1); assert(order.currentPosition() == 4);
    order.reset(std::vector<uint32_t>{UINT32_MAX, UINT32_MAX}); assert(order.first() == -1); assert(order.next() == -1);
    std::cout << "Playlist persistence, validation, power-loss recovery, limits, and scoped playback passed\n";
}
