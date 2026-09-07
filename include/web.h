#pragma once
#include "app.h"
#include "display.h"
#include <WebServer.h>

namespace ct {
class Web {
public:
    Web(App& app, Display& display) : app_(app), display_(display), server_(80) {}
    void begin();
    void tick();
private:
    App& app_;
    Display& display_;
    WebServer server_;
    File upload_;
    String uploadPath_, uploadError_, uploadTemp_;
    uint32_t expected_ = 0, received_ = 0, rebootAt_ = 0;
    bool uploadAllowed_ = false, firmware_ = false, playlistUpload_ = false;
    uint32_t importId_ = 0;
    int uploadStatus_ = 400;
    bool authorized();
    void sendJson(int code, cJSON* json);
    void error(int code, const String& message);
    void uploadChunk(bool firmware, bool playlist = false);
    void finishUpload();
    void screenshot();
    void cover();
};
}
