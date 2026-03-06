#include "ui_highlights.h"
#include "app.h"
#include <stdio.h>
#include <string.h>

#define ENTRY_HEIGHT 40.0f
#define HEADER_HEIGHT 24.0f
#define VISIBLE_ENTRIES ((int)((BOT_SCREEN_HEIGHT - HEADER_HEIGHT) / ENTRY_HEIGHT))

void highlights_view_init(HighlightsViewState* state,
                          HighlightStore* store,
                          const EpubBook* book) {
    state->store = store;
    state->book = book;
    state->selected = 0;
    state->scroll_offset = 0;
    state->needs_redraw = true;
    state->delete_confirm = false;
    state->jump_requested = false;
    state->jump_chapter = 0;
    state->jump_offset = 0;
}

bool highlights_view_update(HighlightsViewState* state,
                            u32 kDown, u32 kHeld, touchPosition* touch) {
    (void)kHeld;
    (void)touch;

    if (state->store->count == 0) {
        // No highlights — B to go back
        if (kDown & KEY_B)
            return true;
        return false;
    }

    // Navigate
    if (kDown & KEY_DUP) {
        state->delete_confirm = false;
        if (state->selected > 0)
            state->selected--;
        state->needs_redraw = true;
    }
    if (kDown & KEY_DDOWN) {
        state->delete_confirm = false;
        if (state->selected < state->store->count - 1)
            state->selected++;
        state->needs_redraw = true;
    }

    // Keep selected item visible
    if (state->selected < state->scroll_offset)
        state->scroll_offset = state->selected;
    if (state->selected >= state->scroll_offset + VISIBLE_ENTRIES)
        state->scroll_offset = state->selected - VISIBLE_ENTRIES + 1;

    // Jump to highlight
    if (kDown & KEY_A) {
        Highlight* h = &state->store->items[state->selected];
        state->jump_requested = true;
        state->jump_chapter = h->chapter;
        state->jump_offset = h->start_offset;
        return true;
    }

    // Delete highlight (two-press confirm)
    if (kDown & KEY_Y) {
        if (state->delete_confirm) {
            highlight_remove(state->store, state->selected);
            state->delete_confirm = false;
            if (state->selected >= state->store->count && state->store->count > 0)
                state->selected = state->store->count - 1;
        } else {
            state->delete_confirm = true;
        }
        state->needs_redraw = true;
    }

    // Back
    if (kDown & KEY_B)
        return true;

    return false;
}

void highlights_view_draw_top(HighlightsViewState* state, C2D_TextBuf buf) {
    C2D_Text text;
    char info[256];

    // Book title
    C2D_TextParse(&text, buf, state->book->title);
    C2D_TextOptimize(&text);
    C2D_DrawText(&text, C2D_WithColor | C2D_AlignCenter | C2D_WordWrap,
                 TOP_SCREEN_WIDTH / 2.0f, 42.0f, 0.5f,
                 0.45f, 0.45f, CLR_TEXT_WHITE,
                 TOP_SCREEN_WIDTH - 40.0f);

    // Highlight count
    snprintf(info, sizeof(info), "%d Highlight%s",
             state->store->count,
             state->store->count == 1 ? "" : "s");
    C2D_TextParse(&text, buf, info);
    C2D_TextOptimize(&text);
    C2D_DrawText(&text, C2D_WithColor | C2D_AlignCenter,
                 TOP_SCREEN_WIDTH / 2.0f, 80.0f, 0.5f,
                 0.4f, 0.4f, CLR_ACCENT);

    // Controls
    const char* help = state->delete_confirm
        ? "Y: Confirm Delete"
        : "D-pad:Nav  A:Jump  Y:Delete  B:Back";
    C2D_TextParse(&text, buf, help);
    C2D_TextOptimize(&text);
    C2D_DrawText(&text, C2D_WithColor | C2D_AlignCenter,
                 TOP_SCREEN_WIDTH / 2.0f, 170.0f, 0.5f,
                 0.35f, 0.35f, CLR_TEXT_WHITE);
}

void highlights_view_draw_bottom(HighlightsViewState* state, C2D_TextBuf buf) {
    // Header bar
    C2D_DrawRectSolid(0, 0, 0.5f, BOT_SCREEN_WIDTH, HEADER_HEIGHT, CLR_ACCENT);
    C2D_Text header;
    char htxt[64];
    snprintf(htxt, sizeof(htxt), "Highlights (%d)", state->store->count);
    C2D_TextParse(&header, buf, htxt);
    C2D_TextOptimize(&header);
    C2D_DrawText(&header, C2D_WithColor, 8.0f, 4.0f, 0.5f,
                 0.45f, 0.45f, CLR_TEXT_WHITE);

    if (state->store->count == 0) {
        C2D_Text empty;
        C2D_TextParse(&empty, buf, "No highlights yet.\nHold touch on text to select.");
        C2D_TextOptimize(&empty);
        C2D_DrawText(&empty, C2D_WithColor | C2D_AlignCenter,
                     BOT_SCREEN_WIDTH / 2.0f, 100.0f, 0.5f,
                     0.4f, 0.4f, CLR_TEXT_BLACK);
        return;
    }

    // Draw visible entries
    float y = HEADER_HEIGHT;
    for (int i = state->scroll_offset;
         i < state->store->count && i < state->scroll_offset + VISIBLE_ENTRIES;
         i++) {
        Highlight* h = &state->store->items[i];

        // Selection highlight
        if (i == state->selected) {
            C2D_DrawRectSolid(0, y, 0.4f, BOT_SCREEN_WIDTH, ENTRY_HEIGHT,
                              CLR_SELECTED);
        }

        // Divider
        C2D_DrawRectSolid(0, y + ENTRY_HEIGHT - 1, 0.5f,
                          BOT_SCREEN_WIDTH, 1, CLR_DIVIDER);

        // Chapter name (small, gray)
        C2D_Text ch_text;
        char ch_str[128];
        if (h->chapter < state->book->chapter_count) {
            snprintf(ch_str, sizeof(ch_str), "Ch.%d: %s",
                     h->chapter + 1,
                     state->book->chapter_names[h->chapter]);
        } else {
            snprintf(ch_str, sizeof(ch_str), "Chapter %d", h->chapter + 1);
        }
        C2D_TextParse(&ch_text, buf, ch_str);
        C2D_TextOptimize(&ch_text);
        C2D_DrawText(&ch_text, C2D_WithColor, 8.0f, y + 2, 0.5f,
                     0.3f, 0.3f, CLR_ACCENT);

        // Snippet (truncated to fit)
        C2D_Text sn_text;
        char sn_str[128];
        // Show first ~60 chars of snippet
        int sn_len = (int)strlen(h->snippet);
        if (sn_len > 60) {
            snprintf(sn_str, sizeof(sn_str), "\"%.57s...\"", h->snippet);
        } else {
            snprintf(sn_str, sizeof(sn_str), "\"%s\"", h->snippet);
        }
        C2D_TextParse(&sn_text, buf, sn_str);
        C2D_TextOptimize(&sn_text);
        u32 sn_color = (i == state->selected) ? CLR_TEXT_WHITE : CLR_TEXT_BLACK;
        C2D_DrawText(&sn_text, C2D_WithColor, 8.0f, y + 16, 0.5f,
                     0.35f, 0.35f, sn_color);

        y += ENTRY_HEIGHT;
    }
}
