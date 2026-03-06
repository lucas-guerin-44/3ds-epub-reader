#include "ui_library.h"
#include "app.h"
#include "config.h"
#include <stdio.h>
#include <string.h>

static void strip_extension(char* dest, const char* src, size_t dest_size) {
    strncpy(dest, src, dest_size - 1);
    dest[dest_size - 1] = '\0';
    char* dot = strrchr(dest, '.');
    if (dot)
        *dot = '\0';
}

void library_init(LibraryState* lib) {
    memset(lib, 0, sizeof(LibraryState));
    lib->needs_refresh = true;
}

void library_refresh(LibraryState* lib) {
    static char filenames[MAX_BOOKS][MAX_FILENAME_LEN];
    lib->book_count = util_scan_dir(BOOKS_DIR, ".epub", filenames, MAX_BOOKS);

    for (int i = 0; i < lib->book_count; i++) {
        strncpy(lib->books[i].filename, filenames[i], MAX_FILENAME_LEN - 1);
        util_path_join(lib->books[i].filepath, BOOKS_DIR, filenames[i]);
        strip_extension(lib->books[i].display_name, filenames[i],
                        MAX_FILENAME_LEN);
        lib->books[i].saved_chapter = -1;
        lib->books[i].saved_page = 0;
        progress_load(lib->books[i].filepath,
                      &lib->books[i].saved_chapter,
                      &lib->books[i].saved_page);
    }

    if (lib->selected >= lib->book_count)
        lib->selected = lib->book_count > 0 ? lib->book_count - 1 : 0;

    lib->needs_refresh = false;
}

int library_update(LibraryState* lib, u32 kDown, touchPosition* touch) {
    if (lib->needs_refresh)
        library_refresh(lib);

    if (lib->book_count == 0)
        return -1;

    // D-pad navigation
    if (kDown & KEY_DUP) {
        if (lib->selected > 0)
            lib->selected--;
    }
    if (kDown & KEY_DDOWN) {
        if (lib->selected < lib->book_count - 1)
            lib->selected++;
    }

    // Keep selection visible
    int visible_items = (int)((BOT_SCREEN_HEIGHT - LIST_TOP) / ITEM_HEIGHT);
    if (lib->selected < lib->scroll_offset)
        lib->scroll_offset = lib->selected;
    if (lib->selected >= lib->scroll_offset + visible_items)
        lib->scroll_offset = lib->selected - visible_items + 1;

    // Touch selection
    if (kDown & KEY_TOUCH) {
        if (touch->py >= (u16)LIST_TOP) {
            int idx = lib->scroll_offset +
                      (int)((touch->py - LIST_TOP) / ITEM_HEIGHT);
            if (idx >= 0 && idx < lib->book_count) {
                if (idx == lib->selected) {
                    // Tap on already-selected item -> open it
                    return lib->selected;
                }
                lib->selected = idx;
            }
        }
    }

    // A button to open
    if (kDown & KEY_A) {
        return lib->selected;
    }

    return -1;
}

void library_draw_top(LibraryState* lib, C2D_TextBuf buf) {
    C2D_Text text;

    if (lib->book_count > 0 && lib->selected < lib->book_count) {
        // Show selected book info
        C2D_TextParse(&text, buf, lib->books[lib->selected].display_name);
        C2D_TextOptimize(&text);
        C2D_DrawText(&text, C2D_WithColor | C2D_AlignCenter,
                     TOP_SCREEN_WIDTH / 2.0f, 60.0f, 0.5f,
                     0.6f, 0.6f, CLR_TEXT_WHITE);

        char info[128];
        snprintf(info, sizeof(info), "Book %d of %d",
                 lib->selected + 1, lib->book_count);
        C2D_TextParse(&text, buf, info);
        C2D_TextOptimize(&text);
        C2D_DrawText(&text, C2D_WithColor | C2D_AlignCenter,
                     TOP_SCREEN_WIDTH / 2.0f, 100.0f, 0.5f,
                     0.45f, 0.45f, CLR_ACCENT);

        // Show saved progress
        if (lib->books[lib->selected].saved_chapter >= 0) {
            char pinfo[64];
            snprintf(pinfo, sizeof(pinfo), "Progress: Chapter %d, Page %d",
                     lib->books[lib->selected].saved_chapter + 1,
                     lib->books[lib->selected].saved_page + 1);
            C2D_TextParse(&text, buf, pinfo);
            C2D_TextOptimize(&text);
            C2D_DrawText(&text, C2D_WithColor | C2D_AlignCenter,
                         TOP_SCREEN_WIDTH / 2.0f, 125.0f, 0.5f,
                         0.4f, 0.4f, CLR_TEXT_WHITE);
        }

        C2D_TextParse(&text, buf, "Press A to read");
        C2D_TextOptimize(&text);
        C2D_DrawText(&text, C2D_WithColor | C2D_AlignCenter,
                     TOP_SCREEN_WIDTH / 2.0f, 150.0f, 0.5f,
                     0.45f, 0.45f, CLR_TEXT_WHITE);
    } else {
        C2D_TextParse(&text, buf, "No books in library");
        C2D_TextOptimize(&text);
        C2D_DrawText(&text, C2D_WithColor | C2D_AlignCenter,
                     TOP_SCREEN_WIDTH / 2.0f, 80.0f, 0.5f,
                     0.55f, 0.55f, CLR_TEXT_WHITE);

        C2D_TextParse(&text, buf,
            "Press X to open transfer mode\n"
            "and upload EPUBs from your PC");
        C2D_TextOptimize(&text);
        C2D_DrawText(&text, C2D_WithColor | C2D_AlignCenter,
                     TOP_SCREEN_WIDTH / 2.0f, 120.0f, 0.5f,
                     0.4f, 0.4f, CLR_ACCENT);
    }
}

void library_draw_bottom(LibraryState* lib, C2D_TextBuf buf) {
    C2D_Text text;

    // Header bar
    C2D_DrawRectSolid(0, 0, 0.5f, BOT_SCREEN_WIDTH, 24, CLR_ACCENT);
    char header[64];
    snprintf(header, sizeof(header), "Library (%d)", lib->book_count);
    C2D_TextParse(&text, buf, header);
    C2D_TextOptimize(&text);
    C2D_DrawText(&text, C2D_WithColor, 8.0f, 4.0f, 0.5f,
                 0.5f, 0.5f, CLR_TEXT_WHITE);

    if (lib->book_count == 0) {
        C2D_TextParse(&text, buf,
            "No books yet!\n\n"
            "Press X to open\n"
            "transfer mode and\n"
            "upload EPUBs.");
        C2D_TextOptimize(&text);
        C2D_DrawText(&text, C2D_WithColor | C2D_AlignCenter,
                     BOT_SCREEN_WIDTH / 2.0f, 80.0f, 0.5f,
                     0.5f, 0.5f, CLR_TEXT_BLACK);
        return;
    }

    // Book list
    int visible_items = (int)((BOT_SCREEN_HEIGHT - LIST_TOP) / ITEM_HEIGHT);
    for (int i = 0; i < visible_items && (lib->scroll_offset + i) < lib->book_count; i++) {
        int idx = lib->scroll_offset + i;
        float y = LIST_TOP + i * ITEM_HEIGHT;

        // Highlight selected item
        if (idx == lib->selected) {
            C2D_DrawRectSolid(0, y, 0.4f, BOT_SCREEN_WIDTH, ITEM_HEIGHT,
                              CLR_ACCENT);
        }

        // Divider line
        C2D_DrawRectSolid(0, y + ITEM_HEIGHT - 1, 0.5f,
                          BOT_SCREEN_WIDTH, 1,
                          C2D_Color32(0xE0, 0xE0, 0xE0, 0x40));

        // Book name
        C2D_TextParse(&text, buf, lib->books[idx].display_name);
        C2D_TextOptimize(&text);
        u32 color = (idx == lib->selected) ? CLR_TEXT_WHITE : CLR_TEXT_BLACK;
        C2D_DrawText(&text, C2D_WithColor, 10.0f, y + 6.0f, 0.5f,
                     0.45f, 0.45f, color);

        // Reading progress indicator
        if (lib->books[idx].saved_chapter >= 0) {
            char prog[24];
            snprintf(prog, sizeof(prog), "Ch.%d p.%d",
                     lib->books[idx].saved_chapter + 1,
                     lib->books[idx].saved_page + 1);
            C2D_TextParse(&text, buf, prog);
            C2D_TextOptimize(&text);
            u32 pcol = (idx == lib->selected)
                       ? C2D_Color32(0xCC, 0xCC, 0xFF, 0xFF)
                       : C2D_Color32(0x80, 0x80, 0x80, 0xFF);
            C2D_DrawText(&text, C2D_WithColor,
                         BOT_SCREEN_WIDTH - 80.0f, y + 8.0f, 0.5f,
                         0.35f, 0.35f, pcol);
        }
    }

    // Scroll indicator
    if (lib->book_count > visible_items) {
        float bar_h = (float)visible_items / lib->book_count *
                      (BOT_SCREEN_HEIGHT - LIST_TOP);
        float bar_y = LIST_TOP + (float)lib->scroll_offset / lib->book_count *
                      (BOT_SCREEN_HEIGHT - LIST_TOP);
        C2D_DrawRectSolid(BOT_SCREEN_WIDTH - 4, bar_y, 0.5f, 4, bar_h,
                          CLR_ACCENT);
    }
}
