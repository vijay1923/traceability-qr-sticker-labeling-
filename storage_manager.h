#ifndef STORAGE_MANAGER_H
#define STORAGE_MANAGER_H

#include <Arduino.h>
#include <LittleFS.h>
#include "config.h"

// Tracks actual mount state. Without this, a failed LittleFS.begin() leaves
// totalBytes()/usedBytes() both reading 0, which a naive free-space check
// would misreport as "almost full" instead of "not mounted".
extern bool littlefs_mounted;

long littlefs_free_bytes();
bool littlefs_check_space_ok(const char *context);

void cmd_listfiles();
void cmd_getfile(const char *filename);

// Destructive - erases every file in /stats (including the checkpoint).
// Never called automatically (e.g. on firmware version change); only ever
// triggered by the explicit "resetdata CONFIRM" command, since this is a
// traceability system and silently losing production history is a real harm.
void reset_all_stats_data();

#endif
