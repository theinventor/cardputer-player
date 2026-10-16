#pragma once

namespace ct {
class WebLifecycle {
public:
    template <typename Server>
    void tick(Server& server, bool connected) {
        // Cold boots with Wi-Fi disabled have no lwIP stack for socket calls.
        if (!connected) {
            if (started_) server.stop();
            started_ = false;
            return;
        }
        if (!started_) {
            server.begin();
            started_ = true;
        }
        server.handleClient();
    }
private:
    bool started_ = false;
};
}
