#include "web_lifecycle.h"
#include <cassert>
#include <cstdio>

struct Server {
    bool networkReady = false, listening = false;
    unsigned starts = 0, stops = 0, requests = 0;
    void begin() {
        assert(networkReady && !listening);
        listening = true;
        ++starts;
    }
    void stop() {
        assert(listening);
        listening = false;
        ++stops;
    }
    void handleClient() {
        assert(networkReady && listening);
        ++requests;
    }
};

int main() {
    ct::WebLifecycle web;
    Server server;
    // Wi-Fi off, missing credentials, or a connection still in progress.
    for (int i = 0; i < 100; ++i) web.tick(server, false);
    assert(server.starts == 0 && server.stops == 0 && server.requests == 0);

    // Connection starts the listener once, without resetting active requests.
    server.networkReady = true;
    for (int i = 0; i < 10; ++i) web.tick(server, true);
    assert(server.starts == 1 && server.stops == 0 && server.requests == 10);

    // Disconnect/disable stops it once; reconnect recreates the listener.
    for (int cycle = 0; cycle < 3; ++cycle) {
        server.networkReady = false;
        for (int i = 0; i < 10; ++i) web.tick(server, false);
        assert(!server.listening && server.stops == unsigned(cycle + 1));
        server.networkReady = true;
        web.tick(server, true);
        assert(server.listening && server.starts == unsigned(cycle + 2));
    }

    ct::WebLifecycle connectedBoot;
    Server online;
    online.networkReady = true;
    connectedBoot.tick(online, true);
    assert(online.starts == 1 && online.requests == 1);
    puts("Web lifecycle tests passed");
}
