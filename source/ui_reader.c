#include "ui_reader.h"
#include "app.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <math.h>

// System font glyph height is 30px
#define GLYPH_HEIGHT 30.0f
// Text buffer size: enough glyphs for one page (~20 lines × ~50 chars)
#define PAGE_TEXT_BUF 2048

static void calc_layout(ReaderState* r) {
    if (r->orientation == ORIENT_HORIZONTAL) {
        r->viewport_w = BOT_SCREEN_WIDTH;
        r->viewport_h = BOT_SCREEN_HEIGHT;
    } else {
        r->viewport_w = BOT_SCREEN_HEIGHT; // 240
        r->viewport_h = BOT_SCREEN_WIDTH;  // 320
    }

    r->line_height = GLYPH_HEIGHT * r->font_scale;
    float usable_h = r->viewport_h - 2 * READER_MARGIN_Y;
    r->lines_per_page = (int)(usable_h / r->line_height);
    if (r->lines_per_page < 1) r->lines_per_page = 1;
}

// Compute page break offsets into chapter text
static void compute_pages(ReaderState* r) {
    if (r->page_offsets) {
        free(r->page_offsets);
        r->page_offsets = NULL;
    }

    float wrap_width = r->viewport_w - 2 * READER_MARGIN_X;
    float avg_char_w = 7.5f * r->font_scale / 0.5f;
    int chars_per_line = (int)(wrap_width / avg_char_w);
    if (chars_per_line < 10) chars_per_line = 10;

    // Generous allocation for page offset array
    int max_pages = r->chapter.length / (chars_per_line > 0 ? chars_per_line : 1) + 100;
    if (max_pages < 16) max_pages = 16;
    r->page_offsets = malloc((max_pages + 2) * sizeof(int));
    if (!r->page_offsets) {
        r->total_pages = 1;
        return;
    }

    r->page_offsets[0] = 0;
    r->total_pages = 1;

    int lines_on_page = 0;
    const char* text = r->chapter.text;
    int text_len = r->chapter.length;
    int pos = 0;

    while (pos < text_len && r->total_pages < max_pages) {
        // Find end of current paragraph
        const char* nl = strchr(text + pos, '\n');
        int para_end = nl ? (int)(nl - text) : text_len;
        int para_len = para_end - pos;

        int visual_lines = (para_len + chars_per_line - 1) / chars_per_line;
        if (visual_lines < 1) visual_lines = 1;

        if (lines_on_page + visual_lines <= r->lines_per_page) {
            // Whole paragraph fits on current page
            lines_on_page += visual_lines;
        } else {
            // Paragraph doesn't fully fit — split it across pages
            int lines_avail = r->lines_per_page - lines_on_page;

            if (lines_avail > 0) {
                // Fill remaining space on this page with part of paragraph
                int chars_fit = lines_avail * chars_per_line;
                int split = pos + chars_fit;
                if (split > para_end) split = para_end;

                // Backtrack to nearest space for clean word break
                for (int i = split; i > pos + chars_fit / 2; i--) {
                    if (text[i] == ' ') { split = i + 1; break; }
                }

                r->page_offsets[r->total_pages++] = split;
                pos = split;
                lines_on_page = 0;
                continue; // re-process remaining text from split point
            } else {
                // Page already full, start new page
                r->page_offsets[r->total_pages++] = pos;
                lines_on_page = 0;
                continue;
            }
        }

        // Advance past the newline
        pos = para_end + (nl ? 1 : 0);
        if (!nl) break;
    }

    // Sentinel: end of text
    r->page_offsets[r->total_pages] = r->chapter.length;
    if (r->total_pages < 1) r->total_pages = 1;
}

static bool load_chapter(ReaderState* r) {
    // Free previous chapter
    epub_free_chapter(&r->chapter);
    r->chapter_loaded = false;

    if (!epub_load_chapter(&r->book, r->current_chapter, &r->chapter))
        return false;

    // Create text buffers if needed
    if (!r->text_buf)
        r->text_buf = C2D_TextBufNew(PAGE_TEXT_BUF);
    if (!r->overlay_buf)
        r->overlay_buf = C2D_TextBufNew(256);

    // Compute page break offsets
    compute_pages(r);

    r->chapter_loaded = true;
    r->rendered_page = -1; // force re-parse on next draw
    return true;
}

bool reader_open(ReaderState* reader, const char* epub_path,
                 int start_chapter, int start_page) {
    memset(reader, 0, sizeof(ReaderState));
    reader->font_scale = READER_FONT_SCALE;
    reader->orientation = ORIENT_HORIZONTAL;
    reader->rendered_page = -1;

    if (!epub_open(epub_path, &reader->book))
        return false;

    reader->book_loaded = true;
    reader->current_chapter = start_chapter;
    reader->current_page = start_page;

    calc_layout(reader);

    // Try loading requested chapter; if it fails, try from chapter 0
    if (!load_chapter(reader)) {
        reader->current_chapter = 0;
        reader->current_page = 0;
        if (!load_chapter(reader))
            return false;
    }

    // Clamp page to valid range
    if (reader->current_page >= reader->total_pages)
        reader->current_page = reader->total_pages - 1;

    return true;
}

void reader_close(ReaderState* reader) {
    epub_free_chapter(&reader->chapter);
    if (reader->text_buf) {
        C2D_TextBufDelete(reader->text_buf);
        reader->text_buf = NULL;
    }
    if (reader->overlay_buf) {
        C2D_TextBufDelete(reader->overlay_buf);
        reader->overlay_buf = NULL;
    }
    if (reader->page_offsets) {
        free(reader->page_offsets);
        reader->page_offsets = NULL;
    }
    reader->book_loaded = false;
    reader->chapter_loaded = false;
}

void reader_relayout(ReaderState* reader) {
    calc_layout(reader);
    compute_pages(reader);
    reader->rendered_page = -1;
    if (reader->current_page >= reader->total_pages)
        reader->current_page = reader->total_pages - 1;
}

static void next_page(ReaderState* r) {
    r->page_turn_timer = 6;
    r->page_turn_dir = 1;
    if (r->current_page < r->total_pages - 1) {
        r->current_page++;
    } else if (r->current_chapter < r->book.chapter_count - 1) {
        r->current_chapter++;
        r->current_page = 0;
        if (!load_chapter(r)) {
            r->current_chapter--;
            r->current_page = r->total_pages - 1;
            load_chapter(r);
        } else {
            r->chapter_changed = true;
        }
    }
}

static void prev_page(ReaderState* r) {
    r->page_turn_timer = 6;
    r->page_turn_dir = -1;
    if (r->current_page > 0) {
        r->current_page--;
    } else if (r->current_chapter > 0) {
        r->current_chapter--;
        r->current_page = 0;
        if (!load_chapter(r)) {
            r->current_chapter++;
            r->current_page = 0;
            load_chapter(r);
        } else {
            r->current_page = r->total_pages - 1;
            r->chapter_changed = true;
        }
    }
}

bool reader_update(ReaderState* reader, u32 kDown, touchPosition* touch) {
    if (!reader->book_loaded)
        return true;

    // D-pad mapping: in vertical mode the 3DS is rotated, so remap
    // Horizontal: Left/Right = page, Up/Down = font
    // Vertical:   Up/Down = page, Left/Right = font
    u32 key_next, key_prev, key_font_up, key_font_down;
    if (reader->orientation == ORIENT_HORIZONTAL) {
        key_next = KEY_DRIGHT;
        key_prev = KEY_DLEFT;
        key_font_up = KEY_DUP;
        key_font_down = KEY_DDOWN;
    } else {
        key_next = KEY_DDOWN;
        key_prev = KEY_DUP;
        key_font_up = KEY_DRIGHT;
        key_font_down = KEY_DLEFT;
    }

    // Page turning
    if (kDown & (KEY_R | key_next))
        next_page(reader);
    if (kDown & (KEY_L | key_prev))
        prev_page(reader);

    // Touch: tap right half = next, left half = prev
    if (kDown & KEY_TOUCH) {
        if (touch->px > BOT_SCREEN_WIDTH / 2)
            next_page(reader);
        else
            prev_page(reader);
    }

    // Orientation toggle
    if (kDown & KEY_Y) {
        reader->orientation = (reader->orientation == ORIENT_HORIZONTAL)
                              ? ORIENT_VERTICAL : ORIENT_HORIZONTAL;
        calc_layout(reader);
        compute_pages(reader);
        reader->rendered_page = -1;
        if (reader->current_page >= reader->total_pages)
            reader->current_page = reader->total_pages - 1;
    }

    // Font size adjustment
    if (kDown & key_font_up) {
        if (reader->font_scale < 0.8f) {
            reader->font_scale += 0.05f;
            calc_layout(reader);
            compute_pages(reader);
            reader->rendered_page = -1;
            reader->font_overlay_timer = 90;
            if (reader->current_page >= reader->total_pages)
                reader->current_page = reader->total_pages - 1;
        }
    }
    if (kDown & key_font_down) {
        if (reader->font_scale > 0.25f) {
            reader->font_scale -= 0.05f;
            calc_layout(reader);
            compute_pages(reader);
            reader->rendered_page = -1;
            reader->font_overlay_timer = 90;
            if (reader->current_page >= reader->total_pages)
                reader->current_page = reader->total_pages - 1;
        }
    }

    // Exit reader
    if (kDown & KEY_B)
        return true;

    // Signal redraw needed for active overlay timers
    reader->needs_redraw = (reader->font_overlay_timer > 0 ||
                            reader->page_turn_timer > 0);

    return false;
}

void reader_draw_top(ReaderState* reader, C2D_TextBuf buf) {
    if (!reader->book_loaded) return;

    C2D_Text text;

    // Book title
    C2D_TextParse(&text, buf, reader->book.title);
    C2D_TextOptimize(&text);
    C2D_DrawText(&text, C2D_WithColor | C2D_AlignCenter | C2D_WordWrap,
                 TOP_SCREEN_WIDTH / 2.0f, 42.0f, 0.5f,
                 0.45f, 0.45f, CLR_TEXT_WHITE,
                 TOP_SCREEN_WIDTH - 40.0f);

    // Chapter info
    char info[256];
    snprintf(info, sizeof(info), "Chapter %d/%d: %s",
             reader->current_chapter + 1,
             reader->book.chapter_count,
             reader->book.chapter_names[reader->current_chapter]);
    C2D_TextParse(&text, buf, info);
    C2D_TextOptimize(&text);
    C2D_DrawText(&text, C2D_WithColor | C2D_AlignCenter,
                 TOP_SCREEN_WIDTH / 2.0f, 80.0f, 0.5f,
                 0.4f, 0.4f, CLR_ACCENT);

    // Page info
    snprintf(info, sizeof(info), "Page %d/%d",
             reader->current_page + 1, reader->total_pages);
    C2D_TextParse(&text, buf, info);
    C2D_TextOptimize(&text);
    C2D_DrawText(&text, C2D_WithColor | C2D_AlignCenter,
                 TOP_SCREEN_WIDTH / 2.0f, 110.0f, 0.5f,
                 0.4f, 0.4f, CLR_TEXT_WHITE);

    // Progress bar
    float bar_w = 200.0f;
    float bar_x = (TOP_SCREEN_WIDTH - bar_w) / 2.0f;
    float bar_y = 138.0f;
    C2D_DrawRectSolid(bar_x, bar_y, 0.5f, bar_w, 6, CLR_DIVIDER);
    float total_chapters = reader->book.chapter_count;
    float progress = (reader->current_chapter +
                      (float)reader->current_page / reader->total_pages) /
                     total_chapters;
    C2D_DrawRectSolid(bar_x, bar_y, 0.5f, bar_w * progress, 6, CLR_ACCENT);

    // Controls help (changes with orientation)
    const char* help = (reader->orientation == ORIENT_HORIZONTAL)
        ? "L/R: Page  Y: Rotate  Up/Down: Font"
        : "L/R: Page  Y: Rotate  Left/Right: Font";
    C2D_TextParse(&text, buf, help);
    C2D_TextOptimize(&text);
    C2D_DrawText(&text, C2D_WithColor | C2D_AlignCenter,
                 TOP_SCREEN_WIDTH / 2.0f, 170.0f, 0.5f,
                 0.35f, 0.35f, CLR_TEXT_WHITE);

    // Orientation indicator
    const char* orient_str = (reader->orientation == ORIENT_HORIZONTAL)
                             ? "Horizontal" : "Vertical";
    C2D_TextParse(&text, buf, orient_str);
    C2D_TextOptimize(&text);
    C2D_DrawText(&text, C2D_WithColor | C2D_AlignCenter,
                 TOP_SCREEN_WIDTH / 2.0f, 190.0f, 0.5f,
                 0.35f, 0.35f, CLR_ACCENT);
}

void reader_draw_bottom(ReaderState* reader) {
    if (!reader->chapter_loaded || !reader->page_offsets || !reader->text_buf)
        return;

    // Re-parse current page text only when page changes
    if (reader->current_page != reader->rendered_page) {
        int start = reader->page_offsets[reader->current_page];
        int end = reader->page_offsets[reader->current_page + 1];
        if (end > reader->chapter.length) end = reader->chapter.length;

        // Temporarily null-terminate the page slice
        char saved = reader->chapter.text[end];
        reader->chapter.text[end] = '\0';

        C2D_TextBufClear(reader->text_buf);
        C2D_TextParse(&reader->rendered_text, reader->text_buf,
                      reader->chapter.text + start);
        C2D_TextOptimize(&reader->rendered_text);

        reader->chapter.text[end] = saved;
        reader->rendered_page = reader->current_page;
    }

    float wrap_w = reader->viewport_w - 2 * READER_MARGIN_X;

    if (reader->orientation == ORIENT_VERTICAL) {
        C2D_ViewRotate(M_PI / 2.0f);
        C2D_ViewTranslate(0, -BOT_SCREEN_WIDTH);
    }

    // Draw current page text
    C2D_DrawText(&reader->rendered_text,
                 C2D_WithColor | C2D_WordWrap,
                 READER_MARGIN_X, READER_MARGIN_Y, 0.5f,
                 reader->font_scale, reader->font_scale,
                 CLR_TEXT_BLACK,
                 wrap_w);

    // Page turn edge flash
    if (reader->page_turn_timer > 0) {
        reader->page_turn_timer--;
        u8 alpha = (u8)(0x60 * reader->page_turn_timer / 6);
        float edge_x = (reader->page_turn_dir > 0)
                        ? reader->viewport_w - 4 : 0;
        C2D_DrawRectSolid(edge_x, 0, 0.9f, 4, reader->viewport_h,
                          C2D_Color32(0x4A, 0x90, 0xD9, alpha));
    }

    C2D_ViewReset();

    // Font size overlay (drawn in screen coords, after ViewReset)
    if (reader->font_overlay_timer > 0) {
        reader->font_overlay_timer--;
        u8 alpha = (reader->font_overlay_timer > 30) ? 0xCC
                   : (u8)(0xCC * reader->font_overlay_timer / 30);
        float ox = BOT_SCREEN_WIDTH / 2.0f - 40;
        float oy = BOT_SCREEN_HEIGHT / 2.0f - 12;
        C2D_DrawRectSolid(ox, oy, 0.95f, 80, 24,
                          C2D_Color32(0x00, 0x00, 0x00, alpha));
        if (reader->overlay_buf) {
            C2D_TextBufClear(reader->overlay_buf);
            C2D_Text ftxt;
            int pct = (int)(reader->font_scale / READER_FONT_SCALE * 100);
            char fstr[16];
            snprintf(fstr, sizeof(fstr), "Font: %d%%", pct);
            C2D_TextParse(&ftxt, reader->overlay_buf, fstr);
            C2D_TextOptimize(&ftxt);
            C2D_DrawText(&ftxt, C2D_WithColor | C2D_AlignCenter,
                         BOT_SCREEN_WIDTH / 2.0f, oy + 4, 1.0f,
                         0.4f, 0.4f,
                         C2D_Color32(0xFF, 0xFF, 0xFF, alpha));
        }
    }
}
