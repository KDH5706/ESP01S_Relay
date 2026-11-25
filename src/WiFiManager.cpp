#include "WiFiManager.h"

static bool apOn = false, pending = false, scanning = false;
static uint32_t applyAt = 0, lastTry = 0;

static String scanCache = "[]";
static uint32_t scanAt = 0;
static bool cached = false;

static IPAddress addr(JsonVariantConst v) {
    IPAddress a;

    a.fromString(v.as<const char*>());

    return a;
}

static void apStart() {
    auto a = cfg["wifi"]["ap"];

    WiFi.mode(WIFI_AP_STA);

    IPAddress ip = addr(a["ip"]);

    bool ok = WiFi.softAPConfig(
        ip,
        ip,
        IPAddress(255, 255, 255, 0)
    );

    apOn = ok && WiFi.softAP(
        a["ssid"].as<const char*>(),
        a["password"].as<const char*>(),
        1,
        false,
        a["maxClients"].as<int>()
    );
}

void wifiApply() {
    WiFi.persistent(false);
    WiFi.mode(WIFI_AP_STA);
    WiFi.disconnect();

    apStart();

    auto s = cfg["wifi"]["sta"];

    WiFi.hostname(cfg["device"]["hostname"].as<const char*>());

    if (s["dhcp"].as<bool>()) {
        WiFi.config(
            IPAddress(0U),
            IPAddress(0U),
            IPAddress(0U)
        );
    } else {
        WiFi.config(
            addr(s["ip"]),
            addr(s["gateway"]),
            addr(s["subnet"]),
            addr(s["dns"])
        );
    }

    WiFi.setAutoReconnect(true);

    if (
        !safeMode &&
        s["enabled"].as<bool>() &&
        strlen(s["ssid"].as<const char*>())
    ) {
        WiFi.begin(
            s["ssid"].as<const char*>(),
            s["password"].as<const char*>()
        );
    }

    lastTry = millis();
}

void wifiSchedule() {
    pending = true;
    applyAt = millis();
}

void wifiUpdate() {
    // Apply pending Wi-Fi configuration
    if (pending && millis() - applyAt >= 750) {
        pending = false;

        wifiApply();
    }

    bool connected = WiFi.status() == WL_CONNECTED;

    // Disable AP when STA is connected
    if (
        !safeMode &&
        !cfg["wifi"]["ap"]["enabled"].as<bool>() &&
        connected &&
        apOn
    ) {
        WiFi.softAPdisconnect(true);

        apOn = false;
    }

    // Restore AP when STA is disconnected
    if (!connected && !apOn) {
        apStart();
    }

    // Retry STA connection
    if (
        !safeMode &&
        !connected &&
        cfg["wifi"]["sta"]["enabled"].as<bool>() &&
        strlen(cfg["wifi"]["sta"]["ssid"].as<const char*>()) &&
        millis() - lastTry >= 20000
    ) {
        lastTry = millis();

        WiFi.reconnect();
    }

    // Process asynchronous Wi-Fi scan
    if (scanning) {
        int n = WiFi.scanComplete();

        if (n >= 0) {
            DynamicJsonDocument d(4096);

            auto a = d.to<JsonArray>();

            for (int j = 0; j < n && j < 24; j++) {
                auto o = a.createNestedObject();

                o["ssid"] = WiFi.SSID(j);
                o["rssi"] = WiFi.RSSI(j);
            }

            scanCache = "";

            serializeJson(d, scanCache);

            WiFi.scanDelete();

            scanning = false;
            cached = true;
            scanAt = millis();
        } else if (n == -2) {
            scanning = false;

            WiFi.scanDelete();
        }
    }
}

void wifiStatus(JsonObject o) {
    bool c = WiFi.status() == WL_CONNECTED;

    o["staConnected"] = c;
    o["staIp"] = c ? WiFi.localIP().toString() : String("");

    if (c) {
        o["rssi"] = WiFi.RSSI();
    } else {
        o["rssi"] = nullptr;
    }

    o["apEnabled"] = apOn;
    o["apIp"] = apOn ? WiFi.softAPIP().toString() : String("");
    o["apClients"] = WiFi.softAPgetStationNum();
}

void wifiScan(JsonObject o) {
    // Return cached scan results
    if (cached && millis() - scanAt < 30000) {
        DynamicJsonDocument d(4096);

        deserializeJson(d, scanCache);

        o["networks"].set(d.as<JsonArray>());
        o["scanning"] = false;

        return;
    }

    // Start asynchronous Wi-Fi scan
    if (!scanning) {
        WiFi.scanDelete();
        WiFi.scanNetworks(true);

        scanning = true;
    }

    o.createNestedArray("networks");
    o["scanning"] = true;
}