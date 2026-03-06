#include "highlight.h"
#include <stdlib.h>
#include <string.h>
#include <c2d/font.h>

// System font glyph height (must match ui_reader.c)
#define GLYPH_HEIGHT 30.0f

// --- Glyph map ---

void glyph_map_build(GlyphMap* map, const char* text, int text_len,
                     float wrap_w, float font_scale,
                     float margin_x, float margin_y,
                     int page_start_offset) {
    // Ensure capacity
    if (map->capacity < text_len) {
        free(map->glyphs);
        map->capacity = text_len + 64;
        map->glyphs = malloc(map->capacity * sizeof(GlyphPos));
        if (!map->glyphs) {
            map->capacity = 0;
            map->count = 0;
            return;
        }
    }
    map->count = 0;

    float line_h = GLYPH_HEIGHT * font_scale;
    float cx = margin_x;
    float cy = margin_y;
    float max_x = margin_x + wrap_w;

    int i = 0;
    while (i < text_len) {
        char c = text[i];

        // Newline: move to next line
        if (c == '\n') {
            i++;
            cx = margin_x;
            cy += line_h;
            continue;
        }

        // Find word boundaries for word-wrap check
        int word_start = i;
        int word_end = i;
        while (word_end < text_len && text[word_end] != ' ' &&
               text[word_end] != '\n' && text[word_end] != '\t') {
            word_end++;
        }

        // Measure word width
        float word_w = 0;
        for (int j = word_start; j < word_end; j++) {
            int gi = C2D_FontGlyphIndexFromCodePoint(NULL, (u32)(unsigned char)text[j]);
            charWidthInfo_s* cwi = C2D_FontGetCharWidthInfo(NULL, gi);
            word_w += cwi->charWidth * font_scale;
        }

        // If word doesn't fit on current line and we're not at line start, wrap
        if (cx > margin_x + 0.01f && cx + word_w > max_x) {
            cx = margin_x;
            cy += line_h;
        }

        // If it's a space/tab, just place it and advance
        if (c == ' ' || c == '\t') {
            int gi = C2D_FontGlyphIndexFromCodePoint(NULL, (u32)(unsigned char)c);
            charWidthInfo_s* cwi = C2D_FontGetCharWidthInfo(NULL, gi);
            float cw = cwi->charWidth * font_scale;

            map->glyphs[map->count++] = (GlyphPos){
                .x = cx, .y = cy, .w = cw, .h = line_h,
                .byte_offset = page_start_offset + i
            };
            cx += cw;
            i++;
            continue;
        }

        // Place each character of the word
        for (int j = word_start; j < word_end; j++) {
            int gi = C2D_FontGlyphIndexFromCodePoint(NULL, (u32)(unsigned char)text[j]);
            charWidthInfo_s* cwi = C2D_FontGetCharWidthInfo(NULL, gi);
            float cw = cwi->charWidth * font_scale;

            // Character-level wrap (word wider than line)
            if (cx + cw > max_x + 0.01f) {
                cx = margin_x;
                cy += line_h;
            }

            map->glyphs[map->count++] = (GlyphPos){
                .x = cx, .y = cy, .w = cw, .h = line_h,
                .byte_offset = page_start_offset + j
            };
            cx += cw;
        }
        i = word_end;
    }
}

void glyph_map_free(GlyphMap* map) {
    free(map->glyphs);
    map->glyphs = NULL;
    map->count = 0;
    map->capacity = 0;
}

int glyph_map_find_at(const GlyphMap* map, float vx, float vy) {
    // Find the closest glyph on the correct line
    float best_dist = 1e9f;
    int best_offset = -1;

    for (int i = 0; i < map->count; i++) {
        const GlyphPos* g = &map->glyphs[i];

        // Must be on the correct line (within line height)
        if (vy < g->y || vy >= g->y + g->h)
            continue;

        // Check horizontal overlap
        if (vx >= g->x && vx < g->x + g->w)
            return g->byte_offset;

        // Track closest on this line
        float dist = (vx < g->x) ? (g->x - vx) : (vx - g->x - g->w);
        if (dist < best_dist) {
            best_dist = dist;
            best_offset = g->byte_offset;
        }
    }

    return best_offset;
}

// --- Word boundary detection ---

void find_word_boundaries(const char* text, int text_len, int pos,
                          int* out_start, int* out_end) {
    if (pos < 0) pos = 0;
    if (pos >= text_len) pos = text_len - 1;
    if (text_len <= 0) {
        *out_start = 0;
        *out_end = 0;
        return;
    }

    // If on whitespace, snap to nearest word
    if (text[pos] == ' ' || text[pos] == '\n' || text[pos] == '\t') {
        // Try forward
        int fwd = pos;
        while (fwd < text_len && (text[fwd] == ' ' || text[fwd] == '\n' || text[fwd] == '\t'))
            fwd++;
        // Try backward
        int bwd = pos;
        while (bwd > 0 && (text[bwd - 1] == ' ' || text[bwd - 1] == '\n' || text[bwd - 1] == '\t'))
            bwd--;

        if (fwd < text_len)
            pos = fwd;
        else if (bwd > 0)
            pos = bwd - 1;
        else {
            *out_start = 0;
            *out_end = text_len;
            return;
        }
    }

    // Scan backward to word start
    int start = pos;
    while (start > 0 && text[start - 1] != ' ' && text[start - 1] != '\n' && text[start - 1] != '\t')
        start--;

    // Scan forward to word end
    int end = pos;
    while (end < text_len && text[end] != ' ' && text[end] != '\n' && text[end] != '\t')
        end++;

    *out_start = start;
    *out_end = end;
}

// --- Highlight rendering ---

void draw_highlight_range(const GlyphMap* map,
                          int range_start, int range_end,
                          u32 color, float z) {
    float rect_x = 0, rect_y = -1, rect_w = 0, rect_h = 0;

    for (int i = 0; i < map->count; i++) {
        int off = map->glyphs[i].byte_offset;
        if (off < range_start || off >= range_end)
            continue;

        float gx = map->glyphs[i].x;
        float gy = map->glyphs[i].y;
        float gw = map->glyphs[i].w;
        float gh = map->glyphs[i].h;

        if (gy != rect_y) {
            // New line — flush previous rect
            if (rect_w > 0)
                C2D_DrawRectSolid(rect_x, rect_y, z, rect_w, rect_h, color);
            rect_x = gx;
            rect_y = gy;
            rect_w = gw;
            rect_h = gh;
        } else {
            // Same line — extend
            rect_w = (gx + gw) - rect_x;
        }
    }
    // Flush last rect
    if (rect_w > 0)
        C2D_DrawRectSolid(rect_x, rect_y, z, rect_w, rect_h, color);
}

// --- Highlight store ---

bool highlight_add(HighlightStore* store, int chapter,
                   int start_offset, int end_offset,
                   const char* chapter_text) {
    if (store->count >= MAX_HIGHLIGHTS)
        return false;

    Highlight* h = &store->items[store->count];
    h->chapter = chapter;
    h->start_offset = start_offset;
    h->end_offset = end_offset;

    // Extract snippet
    int len = end_offset - start_offset;
    if (len > MAX_SNIPPET_LEN - 1) len = MAX_SNIPPET_LEN - 1;
    if (len > 0)
        memcpy(h->snippet, chapter_text + start_offset, len);
    h->snippet[len] = '\0';

    store->count++;
    store->dirty = true;
    return true;
}

void highlight_remove(HighlightStore* store, int index) {
    if (index < 0 || index >= store->count)
        return;

    // Shift remaining items down
    for (int i = index; i < store->count - 1; i++)
        store->items[i] = store->items[i + 1];

    store->count--;
    store->dirty = true;
}
