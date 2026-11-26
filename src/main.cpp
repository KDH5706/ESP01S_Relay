#include "Config.h"
#include "RelayController.h"
#include "WiFiManager.h"
#include "WebServerManager.h"

void setup() {
    relayInit();

    Serial.begin(115200, SERIAL_8N1, SERIAL_TX_ONLY);

    loadConfig();

    if (
        !fsOK ||
        !LittleFS.exists("/index.html") ||
        !LittleFS.exists("/script.js") ||
        !LittleFS.exists("/style.css")
    ) {
        safeMode = true;
    }

    wifiApply();
    webInit();
    relayBoot();

    //Serial.println("Ready");
}

void loop() {
    relayUpdate();
    wifiUpdate();
    webUpdate();
    relayUpdate();

    yield();
}