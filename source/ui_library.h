#ifndef UI_LIBRARY_H
#define UI_LIBRARY_H

#include <3ds.h>
#include <citro2d.h>
#include <stdbool.h>
#include "util.h"

#define MAX_BOOKS 128
#define ITEM_HEIGHT 30.0f
#define LIST_TOP 26.0f

typedef enum {
    SORT_TITLE_AZ,
    SORT_TITLE_ZA,
    SORT_LAST_READ,
    SORT_MODE_COUNT
} SortMode;

typedef struct {
    char filename[MAX_FILENAME_LEN];
    char filepath[MAX_PATH_LEN];
    char display_name[MAX_FILENAME_LEN];
    int  saved_chapter;  // -1 = no progress
    int  saved_page;
    int  last_read_time; // unix timestamp, 0 = never
} BookEntry;

typedef struct {
    BookEntry books[MAX_BOOKS];
    int book_count;
    int selected;
    int scroll_offset;
    bool needs_refresh;
    int  sort_mode;      // SortMode enum
    bool delete_confirm; // waiting for second Y press to confirm delete
} LibraryState;

// Initialize / refresh the book list from SD card
void library_init(LibraryState* lib);
void library_refresh(LibraryState* lib);

// Handle input, returns index of book to open or -1
int library_update(LibraryState* lib, u32 kDown, touchPosition* touch);

// Draw on top and bottom screens
void library_draw_top(LibraryState* lib, C2D_TextBuf buf);
void library_draw_bottom(LibraryState* lib, C2D_TextBuf buf);

#endif
