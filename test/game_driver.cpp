#include "games.h"
#include <cJSON.h>
#include <iostream>
#include <algorithm>
class Painter : public ct::GamePainter {
public:
    cJSON* operations;
    explicit Painter(cJSON* operations) : operations(operations) {}
    void rect(int x, int y, int w, int h, uint32_t color) override { add("rect", "", x, y, w, h, color, 1); }
    void text(const std::string& s, int x, int y, uint32_t color, int scale) override { add("text", s, x, y, 0, 0, color, scale); }
private:
    void add(const char* kind, const std::string& s, int x, int y, int w, int h, uint32_t color, int scale) {
        auto op = cJSON_CreateArray(); cJSON_AddItemToArray(operations, op);
        cJSON_AddItemToArray(op, cJSON_CreateString(kind)); cJSON_AddItemToArray(op, cJSON_CreateString(s.c_str()));
        for (double n : {double(x), double(y), double(w), double(h), double(color), double(scale)}) cJSON_AddItemToArray(op, cJSON_CreateNumber(n));
    }
};
int main() {
    std::srand(42); ct::Games games; int sequence = 0;
    std::string line;
    while (std::getline(std::cin, line)) {
        auto request = cJSON_Parse(line.c_str());
        auto get = [&](const char* key) { auto item = cJSON_GetObjectItem(request, key); return cJSON_IsString(item) ? std::string(item->valuestring) : ""; };
        if (!get("start").empty()) { games.leave(); games.start(ct::gameId(get("start"))); }
        if (!get("key").empty() && !games.input(ct::gameKey(get("key")))) games.leave();
        if (cJSON_IsTrue(cJSON_GetObjectItem(request, "restart"))) games.restart();
        if (cJSON_IsTrue(cJSON_GetObjectItem(request, "pause"))) games.pause();
        auto seq = cJSON_GetObjectItem(request, "sequence");
        if (cJSON_IsNumber(seq)) sequence = seq->valueint;
        games.held(cJSON_IsTrue(cJSON_GetObjectItem(request, "left")), cJSON_IsTrue(cJSON_GetObjectItem(request, "right")));
        auto elapsed = cJSON_GetObjectItem(request, "elapsed");
        if (cJSON_IsNumber(elapsed)) games.tick(std::clamp(elapsed->valueint, 0, 100));
        cJSON_Delete(request);
        auto out = cJSON_CreateObject();
        cJSON_AddNumberToObject(out, "sequence", sequence);
        cJSON_AddStringToObject(out, "game", ct::gameSlug(games.active()));
        cJSON_AddBoolToObject(out, "paused", games.paused());
        cJSON_AddBoolToObject(out, "over", games.over());
        cJSON_AddNumberToObject(out, "score", games.score(games.active()));
        Painter painter(cJSON_AddArrayToObject(out, "ops")); ct::drawGame(games, painter);
        char* raw = cJSON_PrintUnformatted(out); std::cout << (raw ? raw : "{}") << std::endl;
        cJSON_free(raw); cJSON_Delete(out);
    }
}
