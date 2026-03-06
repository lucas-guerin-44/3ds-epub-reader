#include "config.h"
#include "util.h"
#include "cJSON.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

static char* read_json_file(const char* path) {
    FILE* f = fopen(path, "rb");
    if (!f) return NULL;

    fseek(f, 0, SEEK_END);
    long len = ftell(f);
    fseek(f, 0, SEEK_SET);

    if (len <= 0 || len > 1024 * 1024) {
        fclose(f);
        return NULL;
    }

    char* buf = malloc(len + 1);
    if (!buf) { fclose(f); return NULL; }
    fread(buf, 1, len, f);
    buf[len] = '\0';
    fclose(f);
    return buf;
}

static bool write_json_file(const char* path, const char* json_str) {
    FILE* f = fopen(path, "wb");
    if (!f) return false;
    fputs(json_str, f);
    fclose(f);
    return true;
}

static void get_book_key(const char* book_path, char* key, size_t key_size) {
    unsigned int hash = util_hash_string(book_path);
    snprintf(key, key_size, "%08x", hash);
}

bool progress_load(const char* book_path, int* chapter, int* page,
                   float* font_scale, int* orientation,
                   int* dark_mode, int* last_read) {
    char* json_str = read_json_file(PROGRESS_PATH);
    if (!json_str) return false;

    cJSON* root = cJSON_Parse(json_str);
    free(json_str);
    if (!root) return false;

    char key[32];
    get_book_key(book_path, key, sizeof(key));

    cJSON* entry = cJSON_GetObjectItemCaseSensitive(root, key);
    if (!entry) {
        cJSON_Delete(root);
        return false;
    }

    cJSON* ch = cJSON_GetObjectItemCaseSensitive(entry, "chapter");
    cJSON* pg = cJSON_GetObjectItemCaseSensitive(entry, "page");
    cJSON* fs = cJSON_GetObjectItemCaseSensitive(entry, "font_scale");
    cJSON* or_ = cJSON_GetObjectItemCaseSensitive(entry, "orientation");
    cJSON* dm = cJSON_GetObjectItemCaseSensitive(entry, "dark_mode");
    cJSON* lr = cJSON_GetObjectItemCaseSensitive(entry, "last_read");
    if (cJSON_IsNumber(ch)) *chapter = ch->valueint;
    if (cJSON_IsNumber(pg)) *page = pg->valueint;
    if (font_scale && cJSON_IsNumber(fs)) *font_scale = (float)fs->valuedouble;
    if (orientation && cJSON_IsNumber(or_)) *orientation = or_->valueint;
    if (dark_mode && cJSON_IsNumber(dm)) *dark_mode = dm->valueint;
    if (last_read && cJSON_IsNumber(lr)) *last_read = lr->valueint;

    cJSON_Delete(root);
    return true;
}

bool progress_save(const char* book_path, int chapter, int page,
                   float font_scale, int orientation, int dark_mode) {
    // Load existing progress file
    cJSON* root = NULL;
    char* json_str = read_json_file(PROGRESS_PATH);
    if (json_str) {
        root = cJSON_Parse(json_str);
        free(json_str);
    }
    if (!root) {
        root = cJSON_CreateObject();
    }

    char key[32];
    get_book_key(book_path, key, sizeof(key));

    // Remove old entry if exists
    cJSON_DeleteItemFromObjectCaseSensitive(root, key);

    // Add new entry
    cJSON* entry = cJSON_CreateObject();
    cJSON_AddNumberToObject(entry, "chapter", chapter);
    cJSON_AddNumberToObject(entry, "page", page);
    cJSON_AddNumberToObject(entry, "font_scale", font_scale);
    cJSON_AddNumberToObject(entry, "orientation", orientation);
    cJSON_AddNumberToObject(entry, "dark_mode", dark_mode);
    cJSON_AddNumberToObject(entry, "last_read", (double)time(NULL));
    cJSON_AddItemToObject(root, key, entry);

    // Write back
    char* out = cJSON_PrintUnformatted(root);
    bool ok = false;
    if (out) {
        ok = write_json_file(PROGRESS_PATH, out);
        free(out);
    }

    cJSON_Delete(root);
    return ok;
}

bool progress_delete(const char* book_path) {
    cJSON* root = NULL;
    char* json_str = read_json_file(PROGRESS_PATH);
    if (json_str) {
        root = cJSON_Parse(json_str);
        free(json_str);
    }
    if (!root) return false;

    char key[32];
    get_book_key(book_path, key, sizeof(key));
    cJSON_DeleteItemFromObjectCaseSensitive(root, key);

    char* out = cJSON_PrintUnformatted(root);
    bool ok = false;
    if (out) {
        ok = write_json_file(PROGRESS_PATH, out);
        free(out);
    }

    cJSON_Delete(root);
    return ok;
}

// --- Highlight persistence ---

bool highlights_load(const char* book_path, HighlightStore* store) {
    memset(store, 0, sizeof(HighlightStore));

    char* json_str = read_json_file(HIGHLIGHTS_PATH);
    if (!json_str) return false;

    cJSON* root = cJSON_Parse(json_str);
    free(json_str);
    if (!root) return false;

    char key[32];
    get_book_key(book_path, key, sizeof(key));
    cJSON* entry = cJSON_GetObjectItemCaseSensitive(root, key);
    if (!entry) {
        cJSON_Delete(root);
        return false;
    }

    cJSON* arr = cJSON_GetObjectItemCaseSensitive(entry, "highlights");
    if (!cJSON_IsArray(arr)) {
        cJSON_Delete(root);
        return false;
    }

    cJSON* item;
    cJSON_ArrayForEach(item, arr) {
        if (store->count >= MAX_HIGHLIGHTS) break;
        Highlight* h = &store->items[store->count];
        cJSON* ch = cJSON_GetObjectItemCaseSensitive(item, "chapter");
        cJSON* s  = cJSON_GetObjectItemCaseSensitive(item, "start");
        cJSON* e  = cJSON_GetObjectItemCaseSensitive(item, "end");
        cJSON* t  = cJSON_GetObjectItemCaseSensitive(item, "text");
        if (cJSON_IsNumber(ch)) h->chapter = ch->valueint;
        if (cJSON_IsNumber(s))  h->start_offset = s->valueint;
        if (cJSON_IsNumber(e))  h->end_offset = e->valueint;
        if (cJSON_IsString(t)) {
            strncpy(h->snippet, t->valuestring, MAX_SNIPPET_LEN - 1);
            h->snippet[MAX_SNIPPET_LEN - 1] = '\0';
        }
        store->count++;
    }

    cJSON_Delete(root);
    store->dirty = false;
    return store->count > 0;
}

bool highlights_save(const char* book_path, const HighlightStore* store) {
    cJSON* root = NULL;
    char* json_str = read_json_file(HIGHLIGHTS_PATH);
    if (json_str) {
        root = cJSON_Parse(json_str);
        free(json_str);
    }
    if (!root) root = cJSON_CreateObject();

    char key[32];
    get_book_key(book_path, key, sizeof(key));
    cJSON_DeleteItemFromObjectCaseSensitive(root, key);

    cJSON* entry = cJSON_CreateObject();
    cJSON* arr = cJSON_CreateArray();
    for (int i = 0; i < store->count; i++) {
        const Highlight* h = &store->items[i];
        cJSON* item = cJSON_CreateObject();
        cJSON_AddNumberToObject(item, "chapter", h->chapter);
        cJSON_AddNumberToObject(item, "start", h->start_offset);
        cJSON_AddNumberToObject(item, "end", h->end_offset);
        cJSON_AddStringToObject(item, "text", h->snippet);
        cJSON_AddItemToArray(arr, item);
    }
    cJSON_AddItemToObject(entry, "highlights", arr);
    cJSON_AddItemToObject(root, key, entry);

    char* out = cJSON_PrintUnformatted(root);
    bool ok = false;
    if (out) {
        ok = write_json_file(HIGHLIGHTS_PATH, out);
        free(out);
    }

    cJSON_Delete(root);
    return ok;
}

bool highlights_delete(const char* book_path) {
    cJSON* root = NULL;
    char* json_str = read_json_file(HIGHLIGHTS_PATH);
    if (json_str) {
        root = cJSON_Parse(json_str);
        free(json_str);
    }
    if (!root) return false;

    char key[32];
    get_book_key(book_path, key, sizeof(key));
    cJSON_DeleteItemFromObjectCaseSensitive(root, key);

    char* out = cJSON_PrintUnformatted(root);
    bool ok = false;
    if (out) {
        ok = write_json_file(HIGHLIGHTS_PATH, out);
        free(out);
    }

    cJSON_Delete(root);
    return ok;
}

void highlights_export_append(const char* book_title, const char* chapter_name,
                              const char* snippet) {
    FILE* f = fopen(HIGHLIGHTS_EXPORT, "a");
    if (!f) return;
    fprintf(f, "=== %s ===\n%s\n\"%s\"\n---\n", book_title, chapter_name, snippet);
    fclose(f);
}
