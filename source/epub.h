#ifndef EPUB_H
#define EPUB_H

#include <stdbool.h>
#include "util.h"

#define MAX_CHAPTERS 256
#define MAX_TITLE_LEN 256

typedef struct {
    char title[MAX_TITLE_LEN];
    char author[MAX_TITLE_LEN];
    char filepath[MAX_PATH_LEN];      // path to .epub on SD
    char cache_dir[MAX_PATH_LEN];     // path to extracted content
    char opf_dir[MAX_PATH_LEN];       // directory containing the OPF (for relative paths)
    int  chapter_count;
    char chapter_files[MAX_CHAPTERS][MAX_PATH_LEN]; // full paths to XHTML files
    char chapter_names[MAX_CHAPTERS][MAX_TITLE_LEN]; // chapter display names (from NCX or filename)
} EpubBook;

typedef struct {
    char* text;      // extracted plain text (owned, malloc'd)
    int   length;    // length in bytes
} ChapterContent;

// Extract an EPUB file to the cache directory
// Returns true on success
bool epub_extract(const char* epub_path, const char* cache_dir);

// Parse an EPUB: extract, find OPF, read metadata + spine
// epub_path: path to .epub file on SD card
// book: output struct (zeroed before call)
bool epub_open(const char* epub_path, EpubBook* book);

// Load a chapter's XHTML and convert to plain text
// Caller must free content->text when done
bool epub_load_chapter(EpubBook* book, int chapter_index, ChapterContent* content);

// Free chapter content
void epub_free_chapter(ChapterContent* content);

#endif
