#ifndef HIGHLIGHT_H
#define HIGHLIGHT_H

#include <3ds.h>
#include <citro2d.h>
#include <stdbool.h>

// --- Glyph position map (per-page, maps chars to screen coords) ---

typedef struct {
    float x, y;        // top-left of glyph bounding box (viewport coords)
    float w, h;        // glyph width and line height
    int   byte_offset; // absolute offset into chapter.text
} GlyphPos;

typedef struct {
    GlyphPos* glyphs;
    int count;
    int capacity;
} GlyphMap;

// Build glyph position map for a page slice.
// text: pointer to chapter.text + page_start
// text_len: bytes in this page slice
// page_start_offset: absolute byte offset of this page in chapter.text
void glyph_map_build(GlyphMap* map, const char* text, int text_len,
                     float wrap_w, float font_scale,
                     float margin_x, float margin_y,
                     int page_start_offset);

void glyph_map_free(GlyphMap* map);

// Hit-test: find byte_offset of glyph at viewport coords (vx, vy).
// Returns -1 if no glyph found.
int glyph_map_find_at(const GlyphMap* map, float vx, float vy);

// --- Word boundary detection ---

// Given a byte offset into text, find the word containing it.
// out_start/out_end define [start, end) range.
void find_word_boundaries(const char* text, int text_len, int pos,
                          int* out_start, int* out_end);

// --- Highlight rendering ---

// Draw colored rectangles behind a byte range using the glyph map.
void draw_highlight_range(const GlyphMap* map,
                          int range_start, int range_end,
                          u32 color, float z);

// --- Touch state machine ---

typedef enum {
    TOUCH_IDLE,
    TOUCH_DOWN,
    TOUCH_SELECTING
} TouchPhase;

#define TOUCH_HOLD_FRAMES  20   // ~333ms at 60fps

// --- Highlight storage ---

#define MAX_HIGHLIGHTS    256
#define MAX_SNIPPET_LEN   200

typedef struct {
    int  chapter;
    int  start_offset;
    int  end_offset;
    char snippet[MAX_SNIPPET_LEN];
} Highlight;

typedef struct {
    Highlight items[MAX_HIGHLIGHTS];
    int  count;
    bool dirty;
} HighlightStore;

// Add a highlight, extracting snippet from text. Returns true on success.
bool highlight_add(HighlightStore* store, int chapter,
                   int start_offset, int end_offset,
                   const char* chapter_text);

// Remove highlight at index.
void highlight_remove(HighlightStore* store, int index);

// --- Reader action (replaces bool return from reader_update) ---

typedef enum {
    READER_CONTINUE,
    READER_EXIT,
    READER_OPEN_HIGHLIGHTS
} ReaderAction;

#endif
