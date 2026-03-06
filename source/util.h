#ifndef UTIL_H
#define UTIL_H

#include <stdbool.h>
#include <stddef.h>

#define MAX_PATH_LEN 512
#define MAX_FILENAME_LEN 256

// Base path for app data on SD card
#define APP_DIR    "sdmc:/3ds/3ds-epub"
#define BOOKS_DIR  APP_DIR "/books"
#define CACHE_DIR  APP_DIR "/cache"

// Create directory (and parents) if it doesn't exist
bool util_mkdir(const char* path);

// Create all app directories on SD card
bool util_init_dirs(void);

// Scan a directory for files with a given extension
// Returns number of files found (up to max_results)
int util_scan_dir(const char* path, const char* ext,
                  char results[][MAX_FILENAME_LEN], int max_results);

// Check if a file exists
bool util_file_exists(const char* path);

// Join two path components into out (must be at least MAX_PATH_LEN)
void util_path_join(char* out, const char* dir, const char* file);

// Get just the filename from a full path
const char* util_basename(const char* path);

// Simple FNV-1a hash of a string (for cache dir names)
unsigned int util_hash_string(const char* str);

#endif
