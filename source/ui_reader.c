#include "ui_reader.h"
#include "config.h"
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
    if (!r->top_text_buf)
        r->top_text_buf = C2D_TextBufNew(PAGE_TEXT_BUF);

    // Compute page break offsets
    compute_pages(r);

    r->chapter_loaded = true;
    r->rendered_page = -1; // force re-parse on next draw
    r->top_rendered_page = -1;
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
    reader->top_mode = TOP_INFO;
    reader->top_rendered_page = -1;

    // Load saved highlights for this book
    highlights_load(epub_path, &reader->highlights);

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
    // Save highlights if changed
    if (reader->book_loaded && reader->highlights.dirty)
        highlights_save(reader->book.filepath, &reader->highlights);

    epub_free_chapter(&reader->chapter);
    if (reader->text_buf) {
        C2D_TextBufDelete(reader->text_buf);
        reader->text_buf = NULL;
    }
    if (reader->overlay_buf) {
        C2D_TextBufDelete(reader->overlay_buf);
        reader->overlay_buf = NULL;
    }
    if (reader->top_text_buf) {
        C2D_TextBufDelete(reader->top_text_buf);
        reader->top_text_buf = NULL;
    }
    if (reader->page_offsets) {
        free(reader->page_offsets);
        reader->page_offsets = NULL;
    }
    glyph_map_free(&reader->glyph_map);
    reader->book_loaded = false;
    reader->chapter_loaded = false;
}

void reader_relayout(ReaderState* reader) {
    calc_layout(reader);
    compute_pages(reader);
    reader->rendered_page = -1;
    reader->top_rendered_page = -1;
    if (reader->current_page >= reader->total_pages)
        reader->current_page = reader->total_pages - 1;
}

static void next_page(ReaderState* r) {
    r->page_turn_timer = 6;
    r->page_turn_dir = 1;
    r->page_turn_count++;
    int step = (r->top_mode == TOP_DUALPAGE) ? 2 : 1;
    if (r->current_page + step <= r->total_pages - 1) {
        r->current_page += step;
    } else if (r->current_page < r->total_pages - 1) {
        // Partial step: go to last page
        r->current_page = r->total_pages - 1;
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
    r->page_turn_count++;
    int step = (r->top_mode == TOP_DUALPAGE) ? 2 : 1;
    if (r->current_page >= step) {
        r->current_page -= step;
    } else if (r->current_page > 0) {
        // Partial step: go to first page
        r->current_page = 0;
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

// Transform physical touch coordinates to viewport coordinates
static void touch_to_viewport(touchPosition* touch, Orientation orient,
                              float* vx, float* vy) {
    if (orient == ORIENT_VERTICAL) {
        *vx = (float)touch->py;
        *vy = (float)(BOT_SCREEN_WIDTH - touch->px);
    } else {
        *vx = (float)touch->px;
        *vy = (float)touch->py;
    }
}

ReaderAction reader_update(ReaderState* reader, u32 kDown, u32 kHeld, touchPosition* touch) {
    if (!reader->book_loaded)
        return READER_EXIT;

    // Save popup: A to save highlight, B to cancel
    if (reader->show_save_popup) {
        if (kDown & KEY_A) {
            int sel_start = reader->sel_anchor_start < reader->sel_current_start
                            ? reader->sel_anchor_start : reader->sel_current_start;
            int sel_end = reader->sel_anchor_end > reader->sel_current_end
                          ? reader->sel_anchor_end : reader->sel_current_end;
            highlight_add(&reader->highlights, reader->current_chapter,
                          sel_start, sel_end, reader->chapter.text);
            // Also append to plaintext export
            {
                Highlight* last = &reader->highlights.items[reader->highlights.count - 1];
                highlights_export_append(
                    reader->book.title,
                    reader->book.chapter_names[reader->current_chapter],
                    last->snippet);
            }
            reader->has_selection = false;
            reader->show_save_popup = false;
        }
        if (kDown & KEY_B) {
            reader->has_selection = false;
            reader->show_save_popup = false;
        }
        reader->needs_redraw = true;
        return READER_CONTINUE;
    }

    // D-pad mapping: in vertical mode the 3DS is rotated, so remap
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

    // Chapter jump: hold X + D-pad next/prev
    bool chapter_jumped = false;
    if ((kHeld & KEY_X) && (kDown & key_next)) {
        if (reader->current_chapter < reader->book.chapter_count - 1) {
            reader->current_chapter++;
            reader->current_page = 0;
            if (!load_chapter(reader)) {
                reader->current_chapter--;
                load_chapter(reader);
            }
            reader->chapter_changed = true;
        }
        chapter_jumped = true;
    } else if ((kHeld & KEY_X) && (kDown & key_prev)) {
        if (reader->current_chapter > 0) {
            reader->current_chapter--;
            reader->current_page = 0;
            if (!load_chapter(reader)) {
                reader->current_chapter++;
                load_chapter(reader);
            }
            reader->chapter_changed = true;
        }
        chapter_jumped = true;
    }

    // Open highlights viewer: bare X press (no D-pad)
    if (!chapter_jumped && (kDown & KEY_X) &&
        !(kDown & (KEY_DUP | KEY_DDOWN | KEY_DLEFT | KEY_DRIGHT))) {
        return READER_OPEN_HIGHLIGHTS;
    }

    // Page turning (skip if X is held to avoid conflict with chapter jump)
    if (!chapter_jumped && !(kHeld & KEY_X)) {
        if (kDown & (KEY_R | key_next))
            next_page(reader);
        if (kDown & (KEY_L | key_prev))
            prev_page(reader);
    }

    // Touch state machine
    switch (reader->touch_phase) {
        case TOUCH_IDLE:
            if (kDown & KEY_TOUCH) {
                reader->touch_start = *touch;
                reader->touch_down_frames = 0;
                reader->touch_phase = TOUCH_DOWN;
                reader->has_selection = false;
            }
            break;

        case TOUCH_DOWN:
            reader->touch_down_frames++;
            if (!(kHeld & KEY_TOUCH)) {
                // Released — classify as tap (page turn)
                if (reader->touch_down_frames < TOUCH_HOLD_FRAMES) {
                    if (reader->orientation == ORIENT_VERTICAL) {
                        if (reader->touch_start.py > BOT_SCREEN_HEIGHT / 2)
                            next_page(reader);
                        else
                            prev_page(reader);
                    } else {
                        if (reader->touch_start.px > BOT_SCREEN_WIDTH / 2)
                            next_page(reader);
                        else
                            prev_page(reader);
                    }
                }
                reader->touch_phase = TOUCH_IDLE;
            } else if (reader->touch_down_frames >= TOUCH_HOLD_FRAMES) {
                // Long press — start selection
                float vx, vy;
                touch_to_viewport(&reader->touch_start, reader->orientation, &vx, &vy);
                int offset = glyph_map_find_at(&reader->glyph_map, vx, vy);
                if (offset >= 0) {
                    int ws, we;
                    find_word_boundaries(reader->chapter.text, reader->chapter.length,
                                         offset, &ws, &we);
                    reader->sel_anchor_start = ws;
                    reader->sel_anchor_end = we;
                    reader->sel_current_start = ws;
                    reader->sel_current_end = we;
                    reader->has_selection = true;
                    reader->touch_phase = TOUCH_SELECTING;
                    reader->needs_redraw = true;
                } else {
                    reader->touch_phase = TOUCH_IDLE;
                }
            }
            break;

        case TOUCH_SELECTING:
            if (kHeld & KEY_TOUCH) {
                // Drag — update selection endpoint
                float vx, vy;
                touch_to_viewport(touch, reader->orientation, &vx, &vy);
                int offset = glyph_map_find_at(&reader->glyph_map, vx, vy);
                if (offset >= 0) {
                    int ws, we;
                    find_word_boundaries(reader->chapter.text, reader->chapter.length,
                                         offset, &ws, &we);
                    reader->sel_current_start = ws;
                    reader->sel_current_end = we;
                }
                reader->needs_redraw = true;
            } else {
                // Released — show save popup
                reader->show_save_popup = true;
                reader->touch_phase = TOUCH_IDLE;
                reader->needs_redraw = true;
            }
            break;
    }

    // Orientation toggle
    if (kDown & KEY_Y) {
        reader->orientation = (reader->orientation == ORIENT_HORIZONTAL)
                              ? ORIENT_VERTICAL : ORIENT_HORIZONTAL;
        calc_layout(reader);
        compute_pages(reader);
        reader->rendered_page = -1;
        reader->top_rendered_page = -1;
        if (reader->current_page >= reader->total_pages)
            reader->current_page = reader->total_pages - 1;
    }

    // Dark mode toggle
    if (kDown & KEY_A) {
        reader->dark_mode = !reader->dark_mode;
    }

    // Font size adjustment
    if (kDown & key_font_up) {
        if (reader->font_scale < 0.8f) {
            reader->font_scale += 0.05f;
            calc_layout(reader);
            compute_pages(reader);
            reader->rendered_page = -1;
            reader->top_rendered_page = -1;
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
            reader->top_rendered_page = -1;
            reader->font_overlay_timer = 90;
            if (reader->current_page >= reader->total_pages)
                reader->current_page = reader->total_pages - 1;
        }
    }

    // Exit reader
    if (kDown & KEY_B)
        return READER_EXIT;

    // Signal redraw needed for active overlay timers or selection
    reader->needs_redraw = reader->needs_redraw ||
                           (reader->font_overlay_timer > 0) ||
                           (reader->page_turn_timer > 0) ||
                           (reader->touch_phase != TOUCH_IDLE);

    return READER_CONTINUE;
}

void reader_draw_top(ReaderState* reader, C2D_TextBuf buf) {
    if (!reader->book_loaded) return;

    if (reader->top_mode == TOP_DUALPAGE) {
        // Dual-page: top screen shows current_page (read first),
        // bottom screen shows current_page+1 (read second)
        int top_page = reader->current_page;

        // Lazy parse: only when page changes
        if (top_page != reader->top_rendered_page) {
            int start = reader->page_offsets[top_page];
            int end = reader->page_offsets[top_page + 1];
            if (end > reader->chapter.length) end = reader->chapter.length;

            char saved = reader->chapter.text[end];
            reader->chapter.text[end] = '\0';

            C2D_TextBufClear(reader->top_text_buf);
            C2D_TextParse(&reader->top_rendered_text, reader->top_text_buf,
                          reader->chapter.text + start);
            C2D_TextOptimize(&reader->top_rendered_text);

            reader->chapter.text[end] = saved;
            reader->top_rendered_page = top_page;
        }

        // Vertical mode: rotate top screen like bottom screen
        bool vertical = (reader->orientation == ORIENT_VERTICAL);
        if (vertical) {
            C2D_ViewRotate(M_PI / 2.0f);
            C2D_ViewTranslate(0, -TOP_SCREEN_WIDTH);
        }

        // Viewport dimensions after rotation
        float top_vw = vertical ? TOP_SCREEN_HEIGHT : TOP_SCREEN_WIDTH;   // 240 or 400
        float top_vh = vertical ? TOP_SCREEN_WIDTH  : TOP_SCREEN_HEIGHT;  // 400 or 240

        float wrap_w = reader->viewport_w - 2 * READER_MARGIN_X;
        float offset_x = (top_vw - reader->viewport_w) / 2.0f + READER_MARGIN_X;
        // Extra top padding in vertical mode to align with bottom screen
        float offset_y = READER_MARGIN_Y + (vertical ? 16.0f : 0.0f);

        u32 text_color = reader->dark_mode ? CLR_READER_TEXT_LIGHT : CLR_TEXT_BLACK;
        C2D_DrawText(&reader->top_rendered_text,
                     C2D_WithColor | C2D_WordWrap,
                     offset_x, offset_y, 0.5f,
                     reader->font_scale, reader->font_scale,
                     text_color, wrap_w);

        // Small page number at bottom-right
        char pg[32];
        snprintf(pg, sizeof(pg), "%d/%d", top_page + 1, reader->total_pages);
        C2D_Text pgtxt;
        C2D_TextParse(&pgtxt, buf, pg);
        C2D_TextOptimize(&pgtxt);
        u32 pg_clr = reader->dark_mode ? CLR_READER_TEXT_LIGHT : CLR_ACCENT;
        C2D_DrawText(&pgtxt, C2D_WithColor | C2D_AlignRight,
                     top_vw - 8.0f, top_vh - 16.0f, 0.5f,
                     0.3f, 0.3f, pg_clr);

        if (vertical)
            C2D_ViewReset();
        return;
    }

    // TOP_INFO or TOP_OFF: show metadata (TOP_OFF still renders, backlight is just off)
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

    // Controls help
    C2D_TextParse(&text, buf,
        "L/R:Page  A:Theme  Y:Rotate  X:Highlights");
    C2D_TextOptimize(&text);
    C2D_DrawText(&text, C2D_WithColor | C2D_AlignCenter,
                 TOP_SCREEN_WIDTH / 2.0f, 170.0f, 0.5f,
                 0.35f, 0.35f, CLR_TEXT_WHITE);

    // Orientation + theme indicator + top mode hint
    snprintf(info, sizeof(info), "%s  %s  SEL:Dual/Off",
             reader->orientation == ORIENT_HORIZONTAL ? "Horizontal" : "Vertical",
             reader->dark_mode ? "Dark" : "Light");
    C2D_TextParse(&text, buf, info);
    C2D_TextOptimize(&text);
    C2D_DrawText(&text, C2D_WithColor | C2D_AlignCenter,
                 TOP_SCREEN_WIDTH / 2.0f, 190.0f, 0.5f,
                 0.35f, 0.35f, CLR_ACCENT);
}

// Highlight color constants
#define CLR_HIGHLIGHT_ACTIVE  C2D_Color32(0x4A, 0x90, 0xD9, 0x60)
#define CLR_HIGHLIGHT_SAVED   C2D_Color32(0xFF, 0xD7, 0x00, 0x50)

void reader_draw_bottom(ReaderState* reader) {
    if (!reader->chapter_loaded || !reader->page_offsets || !reader->text_buf)
        return;

    // In dual-page mode, bottom screen shows page N+1 (top shows N)
    int display_page = reader->current_page;
    if (reader->top_mode == TOP_DUALPAGE) {
        display_page = reader->current_page + 1;
        if (display_page >= reader->total_pages) {
            // No second page — show end indicator on bottom screen
            if (reader->orientation == ORIENT_VERTICAL) {
                C2D_ViewRotate(M_PI / 2.0f);
                C2D_ViewTranslate(0, -BOT_SCREEN_WIDTH);
            }
            if (reader->overlay_buf) {
                C2D_TextBufClear(reader->overlay_buf);
                C2D_Text etxt;
                C2D_TextParse(&etxt, reader->overlay_buf, "End of chapter");
                C2D_TextOptimize(&etxt);
                u32 clr = reader->dark_mode ? CLR_READER_TEXT_LIGHT : CLR_TEXT_BLACK;
                C2D_DrawText(&etxt, C2D_WithColor | C2D_AlignCenter,
                             reader->viewport_w / 2.0f,
                             reader->viewport_h / 2.0f - 10.0f, 0.5f,
                             0.45f, 0.45f, clr);
            }
            C2D_ViewReset();
            return;
        }
    }

    int page_start = reader->page_offsets[display_page];
    int page_end = reader->page_offsets[display_page + 1];
    if (page_end > reader->chapter.length) page_end = reader->chapter.length;

    // Re-parse current page text and rebuild glyph map when page changes
    if (display_page != reader->rendered_page) {
        // Temporarily null-terminate the page slice
        char saved = reader->chapter.text[page_end];
        reader->chapter.text[page_end] = '\0';

        C2D_TextBufClear(reader->text_buf);
        C2D_TextParse(&reader->rendered_text, reader->text_buf,
                      reader->chapter.text + page_start);
        C2D_TextOptimize(&reader->rendered_text);

        reader->chapter.text[page_end] = saved;

        // Build glyph position map for hit-testing and highlight rendering
        float wrap_w = reader->viewport_w - 2 * READER_MARGIN_X;
        glyph_map_build(&reader->glyph_map,
                        reader->chapter.text + page_start,
                        page_end - page_start,
                        wrap_w, reader->font_scale,
                        READER_MARGIN_X, READER_MARGIN_Y,
                        page_start);

        reader->rendered_page = display_page;
    }

    float wrap_w = reader->viewport_w - 2 * READER_MARGIN_X;

    if (reader->orientation == ORIENT_VERTICAL) {
        C2D_ViewRotate(M_PI / 2.0f);
        C2D_ViewTranslate(0, -BOT_SCREEN_WIDTH);
    }

    // Draw saved highlights (yellow) for current page
    for (int i = 0; i < reader->highlights.count; i++) {
        Highlight* h = &reader->highlights.items[i];
        if (h->chapter != reader->current_chapter) continue;
        if (h->end_offset <= page_start || h->start_offset >= page_end) continue;
        draw_highlight_range(&reader->glyph_map,
                             h->start_offset, h->end_offset,
                             CLR_HIGHLIGHT_SAVED, 0.45f);
    }

    // Draw active selection (blue)
    if (reader->has_selection) {
        int sel_start = reader->sel_anchor_start < reader->sel_current_start
                        ? reader->sel_anchor_start : reader->sel_current_start;
        int sel_end = reader->sel_anchor_end > reader->sel_current_end
                      ? reader->sel_anchor_end : reader->sel_current_end;
        draw_highlight_range(&reader->glyph_map,
                             sel_start, sel_end,
                             CLR_HIGHLIGHT_ACTIVE, 0.45f);
    }

    // Draw current page text (color depends on dark mode)
    u32 text_color = reader->dark_mode ? CLR_READER_TEXT_LIGHT : CLR_TEXT_BLACK;
    C2D_DrawText(&reader->rendered_text,
                 C2D_WithColor | C2D_WordWrap,
                 READER_MARGIN_X, READER_MARGIN_Y, 0.5f,
                 reader->font_scale, reader->font_scale,
                 text_color,
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

    // Save popup overlay (drawn in screen coords)
    if (reader->show_save_popup && reader->overlay_buf) {
        float px = BOT_SCREEN_WIDTH / 2.0f - 60;
        float py = BOT_SCREEN_HEIGHT - 28;
        u32 popup_bg = reader->dark_mode
            ? C2D_Color32(0xFF, 0xFF, 0xFF, 0xDD)
            : C2D_Color32(0x00, 0x00, 0x00, 0xDD);
        u32 popup_fg = reader->dark_mode
            ? C2D_Color32(0x00, 0x00, 0x00, 0xFF)
            : C2D_Color32(0xFF, 0xFF, 0xFF, 0xFF);
        C2D_DrawRectSolid(px, py, 0.95f, 120, 20, popup_bg);
        C2D_TextBufClear(reader->overlay_buf);
        C2D_Text ptxt;
        C2D_TextParse(&ptxt, reader->overlay_buf, "A:Save  B:Cancel");
        C2D_TextOptimize(&ptxt);
        C2D_DrawText(&ptxt, C2D_WithColor | C2D_AlignCenter,
                     BOT_SCREEN_WIDTH / 2.0f, py + 3, 1.0f,
                     0.35f, 0.35f, popup_fg);
    }

    // Font size overlay (drawn in screen coords, after ViewReset)
    if (reader->font_overlay_timer > 0) {
        reader->font_overlay_timer--;
        u8 alpha = (reader->font_overlay_timer > 30) ? 0xCC
                   : (u8)(0xCC * reader->font_overlay_timer / 30);
        float ox = BOT_SCREEN_WIDTH / 2.0f - 40;
        float oy = BOT_SCREEN_HEIGHT / 2.0f - 12;
        // Invert overlay colors in dark mode for visibility
        u32 overlay_bg = reader->dark_mode
            ? C2D_Color32(0xFF, 0xFF, 0xFF, alpha)
            : C2D_Color32(0x00, 0x00, 0x00, alpha);
        u32 overlay_fg = reader->dark_mode
            ? C2D_Color32(0x00, 0x00, 0x00, alpha)
            : C2D_Color32(0xFF, 0xFF, 0xFF, alpha);
        C2D_DrawRectSolid(ox, oy, 0.95f, 80, 24, overlay_bg);
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
                         0.4f, 0.4f, overlay_fg);
        }
    }
}
