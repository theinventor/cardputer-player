#include "web_lifecycle.h"
#include <cassert>
#include <cstdio>

struct Server {
    bool networkReady = false, active = false, attempted = false;
    unsigned starts = 0, stops = 0, requests = 0, failures = 0;
    void begin() {
        assert(networkReady && !attempted);
        attempted = true;
        ++starts;
        if (failures) { --failures; return; }
        active = true;
    }
    bool listening() { return active; }
    void stop() {
        assert(attempted);
        active = attempted = false;
        ++stops;
    }
    void handleClient() {
        assert(networkReady && active);
        ++requests;
    }
};

int main() {
    ct::WebLifecycle web;
    Server server;
    // Wi-Fi off, missing credentials, or a connection still in progress.
    for (int i = 0; i < 100; ++i) web.tick(server, false, i);
    assert(server.starts == 0 && server.stops == 0 && server.requests == 0);

    // Connection starts the listener once, without resetting active requests.
    server.networkReady = true;
    for (int i = 0; i < 10; ++i) web.tick(server, true, 100 + i);
    assert(server.starts == 1 && server.stops == 0 && server.requests == 10);

    // Disconnect/disable stops it once; reconnect recreates the listener.
    for (int cycle = 0; cycle < 3; ++cycle) {
        server.networkReady = false;
        for (int i = 0; i < 10; ++i) web.tick(server, false, 200 + cycle * 100 + i);
        assert(!server.listening() && server.stops == unsigned(cycle + 1));
        server.networkReady = true;
        web.tick(server, true, 250 + cycle * 100);
        assert(server.listening() && server.starts == unsigned(cycle + 2));
    }

    ct::WebLifecycle connectedBoot;
    Server online;
    online.networkReady = true;
    connectedBoot.tick(online, true, 0);
    assert(online.starts == 1 && online.requests == 1);

    // Failed socket creation/bind cleans up and retries without a tight loop.
    ct::WebLifecycle retry;
    Server busy;
    busy.networkReady = true;
    busy.failures = 2;
    retry.tick(busy, true, 0);
    assert(busy.starts == 1 && busy.stops == 1 && busy.requests == 0);
    for (unsigned i = 1; i < 1000; ++i) retry.tick(busy, true, i);
    assert(busy.starts == 1);
    retry.tick(busy, true, 1000);
    assert(busy.starts == 2 && busy.stops == 2 && busy.requests == 0);
    retry.tick(busy, true, 2000);
    assert(busy.starts == 3 && busy.stops == 2 && busy.requests == 1);

    ct::WebLifecycle rollover;
    Server wrapping;
    wrapping.networkReady = true;
    wrapping.failures = 1;
    rollover.tick(wrapping, true, UINT32_MAX - 499);
    rollover.tick(wrapping, true, 499);
    assert(wrapping.starts == 1);
    rollover.tick(wrapping, true, 500);
    assert(wrapping.starts == 2 && wrapping.requests == 1);

    ct::WebLifecycle reconnected;
    Server failed;
    failed.networkReady = true;
    failed.failures = 1;
    reconnected.tick(failed, true, 0);
    failed.networkReady = false;
    reconnected.tick(failed, false, 1);
    failed.networkReady = true;
    reconnected.tick(failed, true, 2);
    assert(failed.starts == 2 && failed.stops == 1 && failed.requests == 1);
    puts("Web lifecycle tests passed");
}
