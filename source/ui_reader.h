#ifndef UI_READER_H
#define UI_READER_H

#include <3ds.h>
#include <citro2d.h>
#include <stdbool.h>
#include "epub.h"
#include "highlight.h"

#define READER_MARGIN_X  6.0f
#define READER_MARGIN_Y  0.0f
#define READER_FONT_SCALE 0.45f

typedef enum {
    ORIENT_HORIZONTAL,  // 320x240 (normal)
    ORIENT_VERTICAL     // 240x320 (rotated)
} Orientation;

typedef enum {
    TOP_INFO,       // Default: metadata, chapter info, controls
    TOP_DUALPAGE,   // Show next page text on top screen
    TOP_OFF         // Top screen backlight off
} TopScreenMode;

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
    bool dark_mode;       // dark background + light text
    int  page_turn_count; // counts page turns for periodic auto-save

    // Dual-page mode (top screen shows next page)
    TopScreenMode  top_mode;
    C2D_TextBuf    top_text_buf;
    C2D_Text       top_rendered_text;
    int            top_rendered_page;  // which page is parsed into top_text_buf (-1 = stale)

    // Text selection & highlighting
    GlyphMap       glyph_map;
    TouchPhase     touch_phase;
    int            touch_down_frames;
    touchPosition  touch_start;
    int            sel_anchor_start, sel_anchor_end;
    int            sel_current_start, sel_current_end;
    bool           has_selection;
    bool           show_save_popup;
    HighlightStore highlights;
} ReaderState;

// Open a book and start reading from the given chapter/page
bool reader_open(ReaderState* reader, const char* epub_path,
                 int start_chapter, int start_page);

// Close the current book
void reader_close(ReaderState* reader);

// Recalculate layout and pages (call after changing font_scale or orientation)
void reader_relayout(ReaderState* reader);

// Handle input. Returns action to take.
ReaderAction reader_update(ReaderState* reader, u32 kDown, u32 kHeld, touchPosition* touch);

// Draw on top and bottom screens
void reader_draw_top(ReaderState* reader, C2D_TextBuf buf);
void reader_draw_bottom(ReaderState* reader);

#endif
