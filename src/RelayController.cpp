#include "RelayController.h"

static constexpr uint8_t RELAY_PIN = 3;

static bool state = false, running = false, dirty = false;
static uint32_t started = 0, active = 0, changed = 0;

static bool inch() {
    return cfg["relay"]["inching"]["enabled"].as<bool>();
}

static void output(bool on) {
    if (state != on) {
        state = on;
        dirty = true;
        changed = millis();
    }

    digitalWrite(RELAY_PIN, on ? LOW : HIGH);
}

void relayInit() {
    digitalWrite(RELAY_PIN, HIGH);
    pinMode(RELAY_PIN, OUTPUT);
}

bool relayIsOn() {
    return state;
}

void relaySet(bool on) {
    if (!on || safeMode) {
        running = false;
        output(false);
        return;
    }

    if (inch()) {
        if (
            running &&
            cfg["relay"]["inching"]["retriggerPolicy"] == "ignore"
        ) {
            return;
        }

        started = millis();
        active = cfg["relay"]["inching"]["durationMs"].as<uint32_t>();
        running = true;
    }

    output(true);
}

void relayBoot() {
    if (!safeMode && !inch()) {
        auto r = cfg["relay"];

        relaySet(
            r["bootMode"] == "ON" ||
            (
                r["bootMode"] == "RESTORE" &&
                r["lastState"].as<bool>()
            )
        );
    }

    dirty = false;
}

void relayUpdate() {
    if (running && uint32_t(millis() - started) >= active) {
        relaySet(false);
    }

    if (
        dirty &&
        !running &&
        uint32_t(millis() - changed) >= 2000
    ) {
        dirty = false;

        if (
            !inch() &&
            cfg["relay"]["bootMode"] == "RESTORE" &&
            cfg["relay"]["lastState"].as<bool>() != state
        ) {
            bool old = cfg["relay"]["lastState"];

            cfg["relay"]["lastState"] = state;

            if (!saveConfig(cfg)) {
                cfg["relay"]["lastState"] = old;
                dirty = true;
                changed = millis();
            }
        }
    }
}

void relayConfigChanged(bool oldInch) {
    if (oldInch != inch()) {
        relaySet(false);
    }
}

void relayStatus(JsonObject o) {
    relayUpdate();

    o["state"] = state;
    o["gpio"] = RELAY_PIN;
    o["level"] = state ? "LOW" : "HIGH";

    auto i = o.createNestedObject("inching");

    i["enabled"] = inch();
    i["durationMs"] = cfg["relay"]["inching"]["durationMs"];
    i["activeDurationMs"] = running ? active : 0;
    i["running"] = running;

    uint32_t elapsed = millis() - started;

    i["remainingMs"] =
        running && elapsed < active ? active - elapsed : 0;
}

void relayShutdown() {
    running = false;
    output(false);
    dirty = false;
}