#ifndef APP_H
#define APP_H

#include <3ds.h>
#include <citro2d.h>
#include "ui_library.h"
#include "ui_reader.h"
#include "ui_highlights.h"
#include "httpd.h"

// Screen dimensions
#define TOP_SCREEN_WIDTH  400
#define TOP_SCREEN_HEIGHT 240
#define BOT_SCREEN_WIDTH  320
#define BOT_SCREEN_HEIGHT 240

// Colors
#define CLR_BG_DARK    C2D_Color32(0x1A, 0x1A, 0x2E, 0xFF)
#define CLR_BG_LIGHT   C2D_Color32(0xFA, 0xF8, 0xF0, 0xFF)
#define CLR_TEXT_WHITE  C2D_Color32(0xFF, 0xFF, 0xFF, 0xFF)
#define CLR_TEXT_BLACK  C2D_Color32(0x10, 0x10, 0x10, 0xFF)
#define CLR_ACCENT      C2D_Color32(0x4A, 0x90, 0xD9, 0xFF)
#define CLR_ACCENT_DARK C2D_Color32(0x2A, 0x60, 0xA0, 0xFF)
#define CLR_SELECTED    C2D_Color32(0x3A, 0x3A, 0x5C, 0xFF)
#define CLR_DIVIDER     C2D_Color32(0x30, 0x30, 0x50, 0xFF)

// Dark reading theme colors
#define CLR_READER_BG_DARK   C2D_Color32(0x1E, 0x1E, 0x1E, 0xFF)
#define CLR_READER_TEXT_LIGHT C2D_Color32(0xD0, 0xCC, 0xC0, 0xFF)

typedef enum {
    SCREEN_LIBRARY,
    SCREEN_READER,
    SCREEN_TRANSFER,
    SCREEN_HIGHLIGHTS
} ScreenID;

typedef struct {
    ScreenID current_screen;

    C3D_RenderTarget* top;
    C3D_RenderTarget* bottom;

    C2D_TextBuf static_buf;
    C2D_TextBuf dynamic_buf;

    // Pre-parsed static text
    C2D_Text title_text;
    C2D_Text help_text;

    // Screen states
    LibraryState library;
    ReaderState  reader;
    HighlightsViewState highlights_view;
    HttpServer   httpd;

    // Error display
    char error_msg[128];
    int  error_timer;  // frames remaining to show error

    // Rendering
    bool needs_redraw;
    bool needs_save;       // deferred save flag (set by APT hook)
    bool needs_lcd_reapply; // re-apply backlight state after resume

    // Loading state
    bool loading;
    int  loading_book_idx;

    // Battery
    u8  battery_level;      // 0-5
    u8  battery_charging;
    int battery_poll_timer;
    bool ptmu_ok;

    // Top screen sleep
    bool top_screen_off;

    // Screen clear colors (change with reader dark mode / dual-page mode)
    u32 bottom_clear_color;
    u32 top_clear_color;

    // System hooks
    aptHookCookie apt_hook;
} AppState;

void app_init(AppState* app);
void app_update(AppState* app, u32 kDown, u32 kHeld, touchPosition* touch);
void app_draw_top(AppState* app);
void app_draw_bottom(AppState* app);
void app_cleanup(AppState* app);

#endif
