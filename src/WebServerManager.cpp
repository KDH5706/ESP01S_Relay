#include "Config.h"
#include "RelayController.h"
#include "WiFiManager.h"
#include "WebServerManager.h"
#include <ESP8266WebServer.h>

static ESP8266WebServer server(80);
static bool rebootPending = false;
static uint32_t rebootAt = 0;

static void sendJson(int code, JsonDocument& d) {
    String s;

    serializeJson(d, s);

    server.sendHeader("Cache-Control", "no-store");
    server.send(code, "application/json", s);
}

static void result(int code, const char* msg) {
    DynamicJsonDocument d(256);

    d["success"] = code < 400;
    d["message"] = msg;

    sendJson(code, d);
}

static bool auth() {
    const char* password = cfg["adminPassword"].as<const char*>();

    if (password && server.authenticate("admin", password)) {
        return true;
    }

    server.sendHeader(
        "WWW-Authenticate",
        "Basic realm=\"ESP01S\""
    );

    result(401, "Authentication required");

    return false;
}

static bool guard(bool write = false) {
    if (!auth()) {
        return false;
    }

    if (rebootPending) {
        result(503, "Restart pending");
        return false;
    }

    if (write) {
        String origin = server.header("Origin"),
               site = server.header("Sec-Fetch-Site");

        if (
            (origin.length() &&
             origin != String("http://") + server.hostHeader()) ||
            site == "cross-site" ||
            site == "same-site"
        ) {
            result(403, "Cross-origin request denied");
            return false;
        }

        if (!server.header("Content-Type").startsWith("application/json")) {
            result(400, "JSON required");
            return false;
        }
    }

    return true;
}

static bool body(JsonDocument& d) {
    String b = server.arg("plain");

    if (b.length() > 16384) {
        result(413, "Body too large");
        return false;
    }

    if (
        deserializeJson(
            d,
            b,
            DeserializationOption::NestingLimit(8)
        ) ||
        !d.is<JsonObject>()
    ) {
        result(400, "Invalid JSON");
        return false;
    }

    return true;
}

static void status() {
    if (!guard()) {
        return;
    }

    DynamicJsonDocument d(1536);

    relayStatus(d.createNestedObject("relay"));
    wifiStatus(d.createNestedObject("wifi"));

    d["system"]["uptime"] = millis() / 1000;

    sendJson(200, d);
}

static void getConfig() {
    if (!guard()) {
        return;
    }

    DynamicJsonDocument d(JSON_CAP);

    d.set(cfg);

    d.remove("adminPassword");
    d["wifi"]["sta"].remove("password");
    d["wifi"]["ap"].remove("password");

    sendJson(200, d);
}

static void putConfig() {
    if (!guard(true)) {
        return;
    }

    DynamicJsonDocument p(JSON_CAP), next(JSON_CAP);

    if (!body(p)) {
        return;
    }

    // Allow older UI clients and backups to omit or send the former AP switch.
    p["wifi"]["ap"].as<JsonObject>().remove("enabled");
    next.set(cfg);

    if (
        !mergePatch(
            next.as<JsonObject>(),
            p.as<JsonObjectConst>()
        ) ||
        next.overflowed() ||
        !validate(next.as<JsonObjectConst>())
    ) {
        result(400, "Invalid configuration");
        return;
    }

    if (next["relay"]["inching"]["enabled"].as<bool>()) {
        next["relay"]["bootMode"] = "OFF";
    }

    String oldNet, newNet;

    serializeJson(cfg["wifi"], oldNet);
    serializeJson(next["wifi"], newNet);

    bool net =
        oldNet != newNet ||
        cfg["device"]["hostname"].as<String>() !=
        next["device"]["hostname"].as<String>();

    String oldAll, newAll;

    serializeJson(cfg, oldAll);
    serializeJson(next, newAll);

    if (oldAll == newAll) {
        result(200, "Unchanged");
        return;
    }

    bool oldInch = cfg["relay"]["inching"]["enabled"];

    if (!saveConfig(next)) {
        result(500, "Save failed");
        return;
    }

    cfg.set(next);

    relayConfigChanged(oldInch);

    result(200, "Saved");

    if (net) {
        wifiSchedule();
    }
}

static void changeAdminPassword() {
    if (!guard(true)) {
        return;
    }

    JsonDocument request;

    if (!body(request)) {
        return;
    }

    const auto current = request["currentPassword"].as<JsonString>();
    const auto replacement = request["newPassword"].as<JsonString>();

    if (request.size() != 2 || !current || !current.size() || !replacement) {
        result(400, "Current and new passwords are required");
        return;
    }

    const auto saved = cfg["adminPassword"].as<JsonString>();

    if (current != saved) {
        result(422, "Current password is incorrect");
        return;
    }

    if (replacement.size() < 4 || replacement.size() > 32) {
        result(400, "New password must be 4 to 32 characters");
        return;
    }

    for (size_t i = 0; i < replacement.size(); ++i) {
        const char c = replacement.c_str()[i];
        if (c < '!' || c > '~') {
            result(400, "New password must use printable ASCII without spaces");
            return;
        }
    }

    if (replacement == saved) {
        result(400, "New password must differ from the current password");
        return;
    }

    JsonDocument next;

    if (!next.set(cfg) || !next["adminPassword"].set(replacement) ||
        next.overflowed() || !saveConfig(next)) {
        result(500, "Save failed");
        return;
    }

    swap(cfg, next);
    result(200, "Password changed");
}

static void staticFile(const char* path, const char* mime) {
    if (!auth()) {
        return;
    }

    if (fsOK && LittleFS.exists(path)) {
        File f = LittleFS.open(path, "r");

        if (f) {
            server.streamFile(f, mime);
            f.close();

            return;
        }
    }

    if (!strcmp(path, "/index.html")) {
        server.send(
            503,
            "text/html; charset=utf-8",
            "<!doctype html>"
            "<meta charset=utf-8>"
            "<h1>ESP01S recovery</h1>"
            "<p>Web files unavailable. Disconnect load, "
            "back up settings and upload LittleFS via UART.</p>"
            "<p><a href='/api/status'>Status</a></p>"
        );
    } else {
        result(404, "File unavailable");
    }
}

void webInit() {
    Serial.println("HTTP user: admin");
    Serial.println("HTTP password: " + cfg["adminPassword"].as<String>());
    Serial.println(
        "AP password: " +
        cfg["wifi"]["ap"]["password"].as<String>()
    );

    server.collectHeaders(
        "Origin", 
        "Sec-Fetch-Site", 
        "Content-Type"
    );

    // Static web files
    server.on("/", HTTP_GET, [] {
        staticFile("/index.html", "text/html; charset=utf-8");
    });

    server.on("/index.html", HTTP_GET, [] {
        staticFile("/index.html", "text/html; charset=utf-8");
    });

    server.on("/style.css", HTTP_GET, [] {
        staticFile("/style.css", "text/css");
    });

    server.on("/script.js", HTTP_GET, [] {
        staticFile("/script.js", "application/javascript");
    });

    // Status and configuration API
    server.on("/api/status", HTTP_GET, status);
    server.on("/api/config", HTTP_GET, getConfig);
    server.on("/api/config", HTTP_PUT, putConfig);
    server.on("/api/system/admin-password", HTTP_PUT, changeAdminPassword);

    // Relay API
    server.on("/api/relay", HTTP_GET, [] {
        if (!guard()) {
            return;
        }

        DynamicJsonDocument d(512);

        relayStatus(d.to<JsonObject>());

        sendJson(200, d);
    });

    server.on("/api/relay", HTTP_POST, [] {
        if (!guard(true)) {
            return;
        }

        DynamicJsonDocument p(256);

        if (!body(p)) {
            return;
        }

        if (p.size() != 1 || !p["state"].is<bool>()) {
            result(400, "Boolean state required");
            return;
        }

        if (safeMode && p["state"].as<bool>()) {
            result(503, "Safe mode");
            return;
        }

        relaySet(p["state"].as<bool>());

        DynamicJsonDocument d(512);

        relayStatus(d.to<JsonObject>());

        d["success"] = true;

        sendJson(200, d);
    });

    // Wi-Fi API
    server.on("/api/wifi/scan", HTTP_GET, [] {
        if (!guard()) {
            return;
        }

        DynamicJsonDocument d(4096);

        wifiScan(d.to<JsonObject>());

        sendJson(200, d);
    });

    server.on("/api/wifi/status", HTTP_GET, [] {
        if (!guard()) {
            return;
        }

        DynamicJsonDocument d(512);

        wifiStatus(d.to<JsonObject>());

        sendJson(200, d);
    });

    // System information API
    server.on("/api/system/info", HTTP_GET, [] {
        if (!guard()) {
            return;
        }

        DynamicJsonDocument d(1024);

        d["firmware"] = "1.1.0-dev";
        d["flashBytes"] = ESP.getFlashChipRealSize();
        d["freeHeap"] = ESP.getFreeHeap();
        d["mac"] = WiFi.macAddress();
        d["resetReason"] = ESP.getResetReason();
        d["safeMode"] = safeMode;

        FSInfo i;

        if (fsOK && LittleFS.info(i)) {
            d["fsTotal"] = i.totalBytes;
            d["fsUsed"] = i.usedBytes;
        }

        sendJson(200, d);
    });

    // System reboot API
    server.on("/api/system/reboot", HTTP_POST, [] {
        if (!guard(true)) {
            return;
        }

        DynamicJsonDocument p(256);

        if (!body(p)) {
            return;
        }

        relayShutdown();

        result(200, "Restarting");

        rebootPending = true;
        rebootAt = millis();
    });

    // Factory reset API
    server.on("/api/system/reset", HTTP_POST, [] {
        if (!guard(true)) {
            return;
        }

        DynamicJsonDocument p(256);

        if (!body(p)) {
            return;
        }

        relayShutdown();

        if (!resetConfig()) {
            result(500, "Reset failed");
            return;
        }

        result(200, "Resetting");

        rebootPending = true;
        rebootAt = millis();
    });

    // Not found handler
    server.onNotFound([] {
        result(404, "Not found");
    });

    server.begin();
}

void webUpdate() {
    server.handleClient();

    if (rebootPending && millis() - rebootAt >= 750) {
        ESP.restart();
    }
}
