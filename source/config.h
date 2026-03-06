#ifndef CONFIG_H
#define CONFIG_H

#include <stdbool.h>
#include "highlight.h"

#define CONFIG_PATH      "sdmc:/3ds/3ds-epub/config.json"
#define PROGRESS_PATH    "sdmc:/3ds/3ds-epub/progress.json"
#define HIGHLIGHTS_PATH  "sdmc:/3ds/3ds-epub/highlights.json"
#define HIGHLIGHTS_EXPORT "sdmc:/3ds/3ds-epub/highlights.txt"

// Load/save reading progress for a book (identified by filepath hash)
bool progress_load(const char* book_path, int* chapter, int* page,
                   float* font_scale, int* orientation,
                   int* dark_mode, int* last_read);
bool progress_save(const char* book_path, int chapter, int page,
                   float font_scale, int orientation, int dark_mode);

// Delete saved progress for a book
bool progress_delete(const char* book_path);

// Load/save/delete highlights for a book
bool highlights_load(const char* book_path, HighlightStore* store);
bool highlights_save(const char* book_path, const HighlightStore* store);
bool highlights_delete(const char* book_path);

// Export a single highlight to the plaintext export file
void highlights_export_append(const char* book_title, const char* chapter_name,
                              const char* snippet);

#endif
