#include "app.h"
#include "display.h"
#include "web.h"

ct::App app;
ct::Display screen(app);
ct::Web web(app, screen);

void setup() {
    Serial.begin(115200);
    // An attached USB host may not consume output. Never stall input on logging.
    Serial.setTxTimeoutMs(1);
    auto config = M5.config();
    config.internal_mic = false;
    M5Cardputer.begin(config);
    M5Cardputer.Display.setRotation(1);
    M5Cardputer.Display.setTextSize(2);
    M5Cardputer.Display.println("Cardtunes");
    app.begin();
    screen.begin();
    web.begin();
    if (app.library.count()) app.play(0);
}

void loop() {
    M5Cardputer.update();
    app.tick();
    screen.tick();
    web.tick();
    delay(1);
}
