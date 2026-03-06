#ifndef CONFIG_H
#define CONFIG_H

#include <stdbool.h>

#define CONFIG_PATH   "sdmc:/3ds/3ds-epub/config.json"
#define PROGRESS_PATH "sdmc:/3ds/3ds-epub/progress.json"

// Load/save reading progress for a book (identified by filepath hash)
bool progress_load(const char* book_path, int* chapter, int* page,
                   float* font_scale, int* orientation);
bool progress_save(const char* book_path, int chapter, int page,
                   float font_scale, int orientation);

#endif
