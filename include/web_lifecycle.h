#pragma once
#include <cstdint>

namespace ct {
class WebLifecycle {
public:
    template <typename Server>
    void tick(Server& server, bool connected, uint32_t now) {
        // Cold boots with Wi-Fi disabled have no lwIP stack for socket calls.
        if (!connected) {
            if (started_) server.stop();
            started_ = false;
            attempted_ = false;
            return;
        }
        if (!started_) {
            if (attempted_ && uint32_t(now - lastAttempt_) < 1000) return;
            attempted_ = true;
            lastAttempt_ = now;
            server.begin();
            started_ = server.listening();
            if (!started_) {
                server.stop();
                return;
            }
        }
        server.handleClient();
    }
private:
    bool started_ = false, attempted_ = false;
    uint32_t lastAttempt_ = 0;
};
}
