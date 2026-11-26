#include "Config.h"

DynamicJsonDocument cfg(JSON_CAP);

bool fsOK = false, safeMode = false;

void defaults(JsonDocument& d) {
    d.clear();

    deserializeJson(d, R"({
        "version": 1,
        "device": {
            "name": "ESP-01S Relay",
            "hostname": "esp01s-relay"
        },
        "wifi": {
            "sta": {
                "enabled": true,
                "ssid": "",
                "password": "",
                "dhcp": true,
                "ip": "192.168.0.100",
                "gateway": "192.168.0.1",
                "subnet": "255.255.255.0",
                "dns": "192.168.0.1"
            },
            "ap": {
                "ssid": "",
                "password": "esp01s-test",
                "ip": "192.168.4.1",
                "maxClients": 4
            }
        },
        "relay": {
            "bootMode": "OFF",
            "lastState": false,
            "inching": {
                "enabled": false,
                "durationMs": 1000,
                "retriggerPolicy": "restart"
            }
        }
    })");

    d["wifi"]["ap"]["ssid"] = String("ESP01S-") + String(ESP.getChipId(), HEX);

    d["adminPassword"] = String("admin");
}

static bool str(JsonVariantConst v, size_t lo, size_t hi) {
    return v.is<const char*>() &&
           strlen(v.as<const char*>()) >= lo &&
           strlen(v.as<const char*>()) <= hi;
}

static bool ip(JsonVariantConst v) {
    IPAddress a;

    return str(v, 7, 15) &&
           a.fromString(v.as<const char*>());
}

static bool one(
    JsonVariantConst v,
    const char* a,
    const char* b,
    const char* c = nullptr
) {
    return str(v, 1, 16) &&
           (
               !strcmp(v.as<const char*>(), a) ||
               !strcmp(v.as<const char*>(), b) ||
               (c && !strcmp(v.as<const char*>(), c))
           );
}

bool validate(JsonObjectConst o) {
    auto d = o["device"],
         s = o["wifi"]["sta"],
         a = o["wifi"]["ap"],
         r = o["relay"],
         i = r["inching"];

    if (
        !str(o["adminPassword"], 4, 32) ||
        !o["version"].is<int>() ||
        o["version"].as<int>() != 1 ||
        !str(d["name"], 1, 64) ||
        !str(d["hostname"], 1, 32)
    ) {
        return false;
    }

    const char* h = d["hostname"];

    for (; *h; h++) {
        if (!isalnum((unsigned char)*h) && *h != '-') {
            return false;
        }
    }

    if (
        !s["enabled"].is<bool>() ||
        !s["dhcp"].is<bool>() ||
        !str(s["ssid"], 0, 32) ||
        !str(s["password"], 0, 63) ||
        !str(a["ssid"], 1, 32) ||
        !str(a["password"], 8, 63) ||
        !ip(a["ip"]) ||
        !a["maxClients"].is<int>() ||
        a["maxClients"].as<int>() < 1 ||
        a["maxClients"].as<int>() > 4
    ) {
        return false;
    }

    for (auto k : {"ip", "gateway", "subnet", "dns"}) {
        if (!ip(s[k])) {
            return false;
        }
    }

    return one(r["bootMode"], "OFF", "ON", "RESTORE") &&
           r["lastState"].is<bool>() &&
           i["enabled"].is<bool>() &&
           i["durationMs"].is<uint32_t>() &&
           i["durationMs"].as<uint32_t>() >= 100 &&
           i["durationMs"].as<uint32_t>() <= 3600000 &&
           one(i["retriggerPolicy"], "restart", "ignore");
}

bool mergePatch(
    JsonObject dst,
    JsonObjectConst src,
    bool publicPatch
) {
    for (JsonPairConst p : src) {
        const char* k = p.key().c_str();

        if (
            !dst.containsKey(k) ||
            (publicPatch && !strcmp(k, "adminPassword"))
        ) {
            return false;
        }

        auto v = p.value();

        if (v.is<JsonObjectConst>()) {
            if (
                !dst[k].is<JsonObject>() ||
                !mergePatch(
                    dst[k].as<JsonObject>(),
                    v.as<JsonObjectConst>(),
                    publicPatch
                )
            ) {
                return false;
            }
        } else {
            if (
                v.isNull() ||
                v.is<JsonArrayConst>() ||
                dst[k].is<JsonObject>()
            ) {
                return false;
            }

            if (publicPatch && !strcmp(k, "lastState")) {
                continue;
            }

            if (
                !strcmp(k, "password") &&
                v.is<const char*>() &&
                !strlen(v.as<const char*>())
            ) {
                continue;
            }

            dst[k].set(v);
        }
    }

    return true;
}

static bool readConfig(const char* path, JsonDocument& d) {
    File f = LittleFS.open(path, "r");

    if (!f || f.size() > 4096) {
        return false;
    }

    d.clear();

    auto e = deserializeJson(
        d,
        f,
        DeserializationOption::NestingLimit(8)
    );

    f.close();

    if (e) {
        return false;
    }

    // Ignore the AP switch stored by older firmware.
    d["wifi"]["ap"].as<JsonObject>().remove("enabled");
    return validate(d.as<JsonObjectConst>());
}

bool saveConfig(JsonDocument& d) {
    if (!fsOK || !validate(d.as<JsonObjectConst>())) {
        return false;
    }

    File f = LittleFS.open("/config.tmp", "w");

    if (!f) {
        return false;
    }

    size_t n = serializeJson(d, f);

    f.flush();
    f.close();

    DynamicJsonDocument check(JSON_CAP);

    if (
        n != measureJson(d) ||
        !readConfig("/config.tmp", check)
    ) {
        return false;
    }

    if (LittleFS.exists("/config.json")) {
        if (readConfig("/config.json", check)) {
            if (
                LittleFS.exists("/config.bak") &&
                !LittleFS.remove("/config.bak")
            ) {
                return false;
            }

            if (!LittleFS.rename("/config.json", "/config.bak")) {
                return false;
            }
        } else if (!LittleFS.remove("/config.json")) {
            return false;
        }
    }

    if (!LittleFS.rename("/config.tmp", "/config.json")) {
        if (LittleFS.exists("/config.bak")) {
            LittleFS.rename("/config.bak", "/config.json");
        }

        return false;
    }

    return true;
}

void loadConfig() {
    LittleFSConfig opts;

    opts.setAutoFormat(false);
    LittleFS.setConfig(opts);

    fsOK = LittleFS.begin();

    if (fsOK && readConfig("/config.json", cfg)) {
        return;
    }

    if (fsOK && readConfig("/config.bak", cfg)) {
        saveConfig(cfg);
        return;
    }

    defaults(cfg);

    safeMode = !fsOK;

    if (fsOK && !saveConfig(cfg)) {
        safeMode = true;
    }
}

bool resetConfig() {
    DynamicJsonDocument d(JSON_CAP);

    defaults(d);

    if (!saveConfig(d)) {
        return false;
    }

    if (
        LittleFS.exists("/config.bak") &&
        !LittleFS.remove("/config.bak")
    ) {
        return false;
    }

    cfg.set(d);

    return true;
}
