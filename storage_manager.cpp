#include <Arduino.h>
#include "storage_manager.h"

bool littlefs_mounted = false;

long littlefs_free_bytes()
{
    return (long)LittleFS.totalBytes() - (long)LittleFS.usedBytes();
}

bool littlefs_check_space_ok(const char *context)
{
    // Distinguish "not mounted" from "full" - they need different fixes
    // (partition table vs. cleanup) and are easy to conflate.
    if (!littlefs_mounted)
    {
        Serial.print("ERR - LittleFS not mounted, skipping write for: ");
        Serial.println(context);
        return false;
    }

    long free_bytes = littlefs_free_bytes();

    if (free_bytes < STATS_FREE_SPACE_MIN_BYTES)
    {
        Serial.print("ERR - LittleFS almost full (");
        Serial.print(free_bytes);
        Serial.print(" bytes free), refusing write for: ");
        Serial.println(context);
        return false;
    }

    if (free_bytes < STATS_FREE_SPACE_WARN_BYTES)
    {
        Serial.print("WARN - LittleFS low on space (");
        Serial.print(free_bytes);
        Serial.print(" bytes free) during: ");
        Serial.println(context);
    }

    return true;
}

void cmd_listfiles()
{
    if (!littlefs_mounted)
    {
        Serial.println("ERR - LittleFS not mounted (check partition scheme has a SPIFFS/littlefs-compatible partition)");
        return;
    }

    File dir = LittleFS.open(STATS_DIR);
    if (!dir || !dir.isDirectory())
    {
        Serial.println("ERR - " STATS_DIR " directory not found");
        return;
    }

    Serial.println("---- FILES in " STATS_DIR " ----");
    File entry = dir.openNextFile();
    while (entry)
    {
        Serial.print(entry.name());
        Serial.print("  (");
        Serial.print(entry.size());
        Serial.println(" bytes)");
        entry = dir.openNextFile();
    }
    Serial.println("--------------------------");
}

// Rejects any filename that could escape the /stats directory or is
// otherwise not a plain filename. Trusted serial console or not, this is a
// near-zero-cost guard. Only used within this module.
static bool filename_is_safe(const char *filename)
{
    if (filename == nullptr || filename[0] == '\0')
        return false;

    for (const char *p = filename; *p != '\0'; p++)
    {
        if (*p == '/' || *p == '\\')
            return false;
    }

    if (strstr(filename, "..") != nullptr)
        return false;

    return true;
}

void cmd_getfile(const char *filename)
{
    if (!filename_is_safe(filename))
    {
        Serial.println("ERR - invalid filename (must be a plain filename, no paths)");
        return;
    }

    if (!littlefs_mounted)
    {
        Serial.println("ERR - LittleFS not mounted (check partition scheme has a SPIFFS/littlefs-compatible partition)");
        return;
    }

    char path[48];
    snprintf(path, sizeof(path), STATS_DIR "/%s", filename);

    File f = LittleFS.open(path, "r");
    if (!f)
    {
        Serial.print("ERR - file not found: ");
        Serial.println(path);
        return;
    }

    size_t file_size = f.size();

    Serial.print("---- BEGIN FILE: ");
    Serial.print(filename);
    Serial.print(" (");
    Serial.print(file_size);
    Serial.println(" bytes) ----");

    // Caps how long we can block loop() dumping a single file. This is a
    // diagnostic command run from a maintenance console, not a bulk export
    // path - if a file legitimately grows beyond this, use several smaller
    // per-month files (already the case) or add chunked/paged retrieval later.
    size_t to_read = file_size;
    bool truncated = false;
    if (to_read > MAX_GETFILE_DUMP_BYTES)
    {
        to_read = MAX_GETFILE_DUMP_BYTES;
        truncated = true;
    }

    for (size_t i = 0; i < to_read && f.available(); i++)
    {
        Serial.write(f.read());
    }

    f.close();
    Serial.println();

    if (truncated)
    {
        Serial.print("---- TRUNCATED at ");
        Serial.print(MAX_GETFILE_DUMP_BYTES);
        Serial.println(" bytes - file is larger, use a smaller/monthly export ----");
    }

    Serial.println("---- END FILE ----");
}

void reset_all_stats_data()
{
    if (!littlefs_mounted)
    {
        Serial.println("ERR - LittleFS not mounted, nothing to wipe");
        return;
    }

    File dir = LittleFS.open(STATS_DIR);
    if (!dir || !dir.isDirectory())
    {
        Serial.println("Nothing to wipe - " STATS_DIR " directory not found");
        return;
    }

    // Collect filenames first, then delete afterward - modifying a directory
    // while still iterating it is asking for trouble on embedded filesystems.
    // Fixed buffers, not String[] - filenames here are always short
    // (MMYY.csv or current.ckp), and this avoids up to 64 heap round-trips
    // in one call.
    const uint8_t MAX_FILES = 64;
    const uint8_t MAX_NAME_LEN = 32;
    char names[MAX_FILES][MAX_NAME_LEN];
    uint8_t name_count = 0;

    File entry = dir.openNextFile();
    while (entry && name_count < MAX_FILES)
    {
        strncpy(names[name_count], entry.name(), MAX_NAME_LEN - 1);
        names[name_count][MAX_NAME_LEN - 1] = '\0';
        name_count++;
        entry = dir.openNextFile();
    }
    dir.close();

    uint16_t deleted_count = 0;
    for (uint8_t i = 0; i < name_count; i++)
    {
        char path[64];
        snprintf(path, sizeof(path), STATS_DIR "/%s", names[i]);

        if (LittleFS.remove(path))
        {
            Serial.print("Deleted: ");
            Serial.println(path);
            deleted_count++;
        }
        else
        {
            Serial.print("ERR - failed to delete: ");
            Serial.println(path);
        }
    }

    Serial.print("OK - resetdata complete, ");
    Serial.print(deleted_count);
    Serial.println(" file(s) deleted");
}
