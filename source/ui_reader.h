#ifndef UI_READER_H
#define UI_READER_H

#include <3ds.h>
#include <citro2d.h>
#include <stdbool.h>
#include "epub.h"

#define READER_MARGIN_X  6.0f
#define READER_MARGIN_Y  0.0f
#define READER_FONT_SCALE 0.45f

typedef enum {
    ORIENT_HORIZONTAL,  // 320x240 (normal)
    ORIENT_VERTICAL     // 240x320 (rotated)
} Orientation;

typedef struct {
    EpubBook    book;
    ChapterContent chapter;

    int  current_chapter;
    int  current_page;
    int  total_pages;
    int  lines_per_page;

    float font_scale;
    float line_height;
    float viewport_w;
    float viewport_h;
    Orientation orientation;

    bool book_loaded;
    bool chapter_loaded;

    // Per-page rendering: page_offsets[i] = byte offset into chapter.text
    // where page i starts. page_offsets[total_pages] = end of text.
    int* page_offsets;
    int  rendered_page;   // which page is currently parsed into text_buf

    C2D_TextBuf text_buf;
    C2D_Text    rendered_text;

    // UI overlays
    C2D_TextBuf overlay_buf;
    int  font_overlay_timer;
    int  page_turn_timer;
    int  page_turn_dir;   // +1 forward, -1 backward
    bool needs_redraw;
    bool chapter_changed; // set when chapter loads, cleared by app after saving
} ReaderState;

// Open a book and start reading from the given chapter/page
bool reader_open(ReaderState* reader, const char* epub_path,
                 int start_chapter, int start_page);

// Close the current book
void reader_close(ReaderState* reader);

// Recalculate layout and pages (call after changing font_scale or orientation)
void reader_relayout(ReaderState* reader);

// Handle input. Returns true if user wants to exit reader.
bool reader_update(ReaderState* reader, u32 kDown, touchPosition* touch);

// Draw on top and bottom screens
void reader_draw_top(ReaderState* reader, C2D_TextBuf buf);
void reader_draw_bottom(ReaderState* reader);

#endif
