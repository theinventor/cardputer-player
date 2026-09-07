#pragma once

// Only the explicit hardware-test environment installs this disposable fixture.
// Slot 16 must be unused; existing playlists and MP3 files are never replaced.
static void installLargePlaylistFixture(ct::App& app) {
    const std::string target = "/.cardtunes/playlist-16.jsonl";
    if (!app.library.mounted() || app.playlistFiles.exists(target) || app.playlistFiles.exists(target + ".bak") ||
        app.playlistFiles.exists("/.cardtunes/playlist-16.json") || app.playlistFiles.exists("/.cardtunes/playlist-16.json.bak")) return;
    auto writer = app.playlistFiles.openWriter(target + ".tmp");
    if (!writer || !writer->write("{\"version\":2,\"name\":\"Cardtunes capacity test\",\"count\":1000}\n")) return;
    for (unsigned i = 0; i < 1000; ++i) {
        std::string path = i == 999 ? "/@demo.mp3" : "/Music/Capacity test/" + std::string(150, 'x') + std::to_string(i) + ".mp3";
        if (!writer->write("\"" + path + "\"\n")) return;
    }
    if (!writer->finish()) return;
    writer.reset();
    if (app.playlistFiles.rename(target + ".tmp", target)) app.playlists.begin();
}
