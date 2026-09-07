#include "storage.h"
#include <cassert>
#include <fstream>
#include <iostream>
#include <set>
#include <unistd.h>

namespace fs = std::filesystem;
void put(const fs::path& path, const std::string& value = "An untagged MP3 fixture") {
    fs::create_directories(path.parent_path());
    std::ofstream out(path, std::ios::binary); out << value; assert(out.good());
}
std::string read(const fs::path& path) {
    std::ifstream in(path, std::ios::binary);
    return {std::istreambuf_iterator<char>(in), {}};
}
void finish(ct::Library& library) {
    unsigned steps = 0;
    while (library.scanning()) { library.scanStep(); assert(++steps < 30000); }
}
int main() {
    auto root = fs::temp_directory_path() / ("cardtunes-scan-" + std::to_string(getpid()));
    fs::create_directories(root);
    ct::Library library(root.c_str());
    put(root / "Music/original.mp3");
    assert(library.begin() && library.scanning());
    assert(library.count() == 0);
    finish(library); assert(library.scanSucceeded() && library.count() == 1);
    auto original = read(root / ".cardtunes/library-v1.bin");

    for (unsigned i = 0; i < 1000; ++i) {
        put(root / ("Music/folder-" + std::to_string(i % 4)) / ("song-" + std::to_string(i) + ".mp3"));
        put(root / ("Music/folder-" + std::to_string(i % 4)) / ("._song-" + std::to_string(i) + ".mp3"));
    }
    put(root / ".Spotlight-V100/hidden.mp3");
    assert(library.scan());
    assert(!library.scan());
    while (library.scanned() < 10) { library.scanStep(); assert(library.count() == 1); }
    assert(!library.append("/Music/original.mp3"));
    library.cancelScan();
    assert(!library.scanning() && !library.scanSucceeded());
    assert(library.scanError() == "Scan cancelled" && library.count() == 1);
    assert(read(root / ".cardtunes/library-v1.bin") == original);
    assert(!fs::exists(root / ".cardtunes/library.tmp"));

    failWriteAfter = 17;
    assert(library.scan()); finish(library);
    assert(!library.scanSucceeded() && !library.scanError().empty());
    assert(read(root / ".cardtunes/library-v1.bin") == original && library.count() == 1);
    failWriteAfter = -1;
    SD.failRename = "/.cardtunes/library.tmp";
    assert(library.scan()); finish(library);
    assert(!library.scanSucceeded() && library.count() == 1);
    assert(read(root / ".cardtunes/library-v1.bin") == original);
    SD.failRename.clear();

    assert(library.scan());
    unsigned steps = 0;
    while (library.scanning()) {
        auto count = library.scanned();
        library.scanStep(); ++steps;
        assert(library.scanned() <= count + 1);
        if (library.scanning()) assert(library.count() == 1);
    }
    assert(steps > 2000 && library.scanSucceeded() && library.count() == 1001);
    assert(library.skipped() == 0);
    std::set<std::string> paths;
    ct::Track track;
    for (unsigned i = 0; i < library.count(); ++i) { assert(library.get(i, track)); assert(paths.insert(track.path).second); }
    assert(paths.count("/Music/original.mp3") && paths.count("/Music/folder-3/song-999.mp3"));
    std::set<std::string> streamed;
    uint32_t expectedId = 0;
    assert(library.each([&](uint32_t id, const ct::Track& value) { assert(id == expectedId++); streamed.insert(value.path); return true; }));
    assert(streamed == paths);
    assert(library.byPath("/Music/original.mp3") >= 0); assert(library.byPath("/Music/absent.mp3") == -1);
    unsigned visits = 0;
    assert(!library.each([&](uint32_t, const ct::Track&) { return ++visits < 3; })); assert(visits == 3);
    ct::Library rebooted(root.c_str());
    assert(rebooted.begin() && !rebooted.scanning() && rebooted.count() == 1001);

    fs::rename(root / ".cardtunes/library-v1.bin", root / ".cardtunes/library.bak");
    put(root / ".cardtunes/library.tmp", "interrupted write");
    ct::Library recovered(root.c_str());
    assert(recovered.begin() && !recovered.scanning() && recovered.count() == 1001);
    for (unsigned i = 1000; i < 10002; ++i) put(root / "Music" / ("song-" + std::to_string(i) + ".mp3"));
    assert(recovered.scan()); finish(recovered);
    assert(recovered.scanSucceeded() && recovered.count() == ct::Library::MaxTracks && recovered.skipped() == 3);
    fs::remove_all(root);
    std::cout << "Incremental scans: 10000 songs, sidecars, cancellation, write failure, rename rollback, reboot recovery passed\n";
}
