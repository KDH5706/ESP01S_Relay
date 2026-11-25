#pragma once

#include "Config.h"

void relayInit();
void relayBoot();
void relaySet(bool on);
void relayShutdown();
void relayUpdate();
void relayStatus(JsonObject o);
void relayConfigChanged(bool oldInch);

bool relayIsOn();