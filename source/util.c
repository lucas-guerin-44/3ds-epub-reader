#include "util.h"
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <dirent.h>
#include <errno.h>

bool util_mkdir(const char* path) {
    struct stat st;
    if (stat(path, &st) == 0)
        return true;
    return mkdir(path, 0755) == 0;
}

bool util_init_dirs(void) {
    util_mkdir(APP_DIR);
    util_mkdir(BOOKS_DIR);
    util_mkdir(CACHE_DIR);
    return true;
}

int util_scan_dir(const char* path, const char* ext,
                  char results[][MAX_FILENAME_LEN], int max_results) {
    DIR* dir = opendir(path);
    if (!dir)
        return 0;

    int count = 0;
    size_t ext_len = ext ? strlen(ext) : 0;
    struct dirent* entry;

    while ((entry = readdir(dir)) != NULL && count < max_results) {
        if (entry->d_name[0] == '.')
            continue;

        if (ext && ext_len > 0) {
            size_t name_len = strlen(entry->d_name);
            if (name_len <= ext_len)
                continue;
            if (strcasecmp(entry->d_name + name_len - ext_len, ext) != 0)
                continue;
        }

        strncpy(results[count], entry->d_name, MAX_FILENAME_LEN - 1);
        results[count][MAX_FILENAME_LEN - 1] = '\0';
        count++;
    }

    closedir(dir);
    return count;
}

bool util_file_exists(const char* path) {
    struct stat st;
    return stat(path, &st) == 0;
}

void util_path_join(char* out, const char* dir, const char* file) {
    snprintf(out, MAX_PATH_LEN, "%s/%s", dir, file);
}

const char* util_basename(const char* path) {
    const char* last_slash = strrchr(path, '/');
    if (last_slash)
        return last_slash + 1;
    return path;
}

bool util_delete_file(const char* path) {
    return remove(path) == 0;
}

unsigned int util_hash_string(const char* str) {
    unsigned int hash = 2166136261u;
    while (*str) {
        hash ^= (unsigned char)*str++;
        hash *= 16777619u;
    }
    return hash;
}
