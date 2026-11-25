#pragma once
#include <Arduino.h>
#include <ArduinoJson.h>
#include <ESP8266WiFi.h>
#include <LittleFS.h>

constexpr size_t JSON_CAP=6144;

extern DynamicJsonDocument cfg;
extern bool fsOK, safeMode;

void defaults(JsonDocument& d);
bool validate(JsonObjectConst o);
bool mergePatch(JsonObject dst, JsonObjectConst src, bool publicPatch=true);
bool saveConfig(JsonDocument& d);
void loadConfig();
bool resetConfig();
