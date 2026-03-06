#ifndef UI_HIGHLIGHTS_H
#define UI_HIGHLIGHTS_H

#include <3ds.h>
#include <citro2d.h>
#include <stdbool.h>
#include "highlight.h"
#include "epub.h"

typedef struct {
    HighlightStore* store;
    const EpubBook* book;
    int  selected;
    int  scroll_offset;
    bool needs_redraw;
    bool delete_confirm;

    // Jump request: set when user presses A on a highlight
    bool jump_requested;
    int  jump_chapter;
    int  jump_offset;
} HighlightsViewState;

// Initialize the highlights viewer
void highlights_view_init(HighlightsViewState* state,
                          HighlightStore* store,
                          const EpubBook* book);

// Handle input. Returns true if user wants to go back.
bool highlights_view_update(HighlightsViewState* state,
                            u32 kDown, u32 kHeld, touchPosition* touch);

// Draw on top and bottom screens
void highlights_view_draw_top(HighlightsViewState* state, C2D_TextBuf buf);
void highlights_view_draw_bottom(HighlightsViewState* state, C2D_TextBuf buf);

#endif
