#include "app.h"
#include "util.h"
#include "config.h"
#include <stdio.h>
#include <string.h>
#include <3ds/services/ptmu.h>
#include <3ds/services/gsplcd.h>

static void save_reader_progress(AppState* app) {
    if (app->reader.book_loaded) {
        progress_save(app->reader.book.filepath,
                      app->reader.current_chapter,
                      app->reader.current_page,
                      app->reader.font_scale,
                      app->reader.orientation);
    }
}

static void apt_hook_callback(APT_HookType hook, void* param) {
    AppState* app = (AppState*)param;
    if (hook == APTHOOK_ONSUSPEND || hook == APTHOOK_ONSLEEP) {
        save_reader_progress(app);
    }
}

void app_init(AppState* app) {
    memset(app, 0, sizeof(AppState));

    app->current_screen = SCREEN_LIBRARY;

    // Create render targets
    app->top    = C2D_CreateScreenTarget(GFX_TOP, GFX_LEFT);
    app->bottom = C2D_CreateScreenTarget(GFX_BOTTOM, GFX_LEFT);

    // Create text buffers
    app->static_buf  = C2D_TextBufNew(4096);
    app->dynamic_buf = C2D_TextBufNew(4096);

    // Parse static text
    C2D_TextParse(&app->title_text, app->static_buf, "3DS EPUB Reader");
    C2D_TextOptimize(&app->title_text);

    C2D_TextParse(&app->help_text, app->static_buf,
        "A: Open  B: Back  X: Transfer  START: Exit");
    C2D_TextOptimize(&app->help_text);

    // Init SD card directories and library
    util_init_dirs();
    library_init(&app->library);

    // Init battery monitoring
    app->ptmu_ok = (R_SUCCEEDED(ptmuInit()));
    app->battery_poll_timer = 0;

    // Init LCD control (for top screen sleep)
    app->gsplcd_ok = (R_SUCCEEDED(gspLcdInit()));

    // Hook suspend/sleep to auto-save progress
    aptHook(&app->apt_hook, apt_hook_callback, app);
}

void app_update(AppState* app, u32 kDown, u32 kHeld, touchPosition* touch) {
    (void)kHeld;

    // Poll battery every ~1 second
    if (app->ptmu_ok && --app->battery_poll_timer <= 0) {
        PTMU_GetBatteryLevel(&app->battery_level);
        PTMU_GetBatteryChargeState(&app->battery_charging);
        app->battery_poll_timer = 60;
        app->needs_redraw = true;
    }

    // Active timers need redraws
    if (app->error_timer > 0 || app->loading)
        app->needs_redraw = true;

    // SELECT: toggle top screen backlight
    if ((kDown & KEY_SELECT) && app->gsplcd_ok) {
        app->top_screen_off = !app->top_screen_off;
        if (app->top_screen_off)
            GSPLCD_PowerOffBacklight(GSPLCD_SCREEN_TOP);
        else
            GSPLCD_PowerOnBacklight(GSPLCD_SCREEN_TOP);
        app->needs_redraw = true;
    }

    switch (app->current_screen) {
        case SCREEN_LIBRARY: {
            // Deferred book open: first frame sets loading flag,
            // second frame actually opens (so "Opening..." is visible)
            if (app->loading) {
                const char* path = app->library.books[app->loading_book_idx].filepath;
                int ch = 0, pg = 0;
                float fs = READER_FONT_SCALE;
                int orient = ORIENT_HORIZONTAL;
                progress_load(path, &ch, &pg, &fs, &orient);
                if (reader_open(&app->reader, path, ch, pg)) {
                    // Restore saved font scale and orientation
                    app->reader.font_scale = fs;
                    app->reader.orientation = orient;
                    reader_relayout(&app->reader);
                    app->current_screen = SCREEN_READER;
                } else {
                    snprintf(app->error_msg, sizeof(app->error_msg),
                             "Failed to open book");
                    app->error_timer = 180;
                }
                app->loading = false;
                break;
            }
            if (kDown & KEY_X) {
                app->current_screen = SCREEN_TRANSFER;
                break;
            }
            int book_idx = library_update(&app->library, kDown, touch);
            if (book_idx >= 0) {
                app->loading = true;
                app->loading_book_idx = book_idx;
            }
            break;
        }

        case SCREEN_READER:
            if (app->reader.needs_redraw)
                app->needs_redraw = true;
            // Auto-save on chapter change
            if (app->reader.chapter_changed) {
                save_reader_progress(app);
                app->reader.chapter_changed = false;
            }
            if (reader_update(&app->reader, kDown, touch)) {
                save_reader_progress(app);
                reader_close(&app->reader);
                app->current_screen = SCREEN_LIBRARY;
                app->library.needs_refresh = true;
            }
            break;

        case SCREEN_TRANSFER:
            // Start server on entering transfer mode
            if (!app->httpd.running) {
                httpd_init(&app->httpd, HTTPD_PORT);
            }
            // Poll for connections (always redraw on transfer screen)
            httpd_poll(&app->httpd);
            app->needs_redraw = true;

            if (kDown & KEY_B) {
                httpd_shutdown(&app->httpd);
                app->current_screen = SCREEN_LIBRARY;
                app->library.needs_refresh = true;
            }
            break;
    }
}

void app_draw_top(AppState* app) {
    // Title always shown at top
    C2D_DrawText(&app->title_text,
        C2D_WithColor | C2D_AlignCenter,
        TOP_SCREEN_WIDTH / 2.0f, 8.0f, 0.5f,
        0.55f, 0.55f, CLR_ACCENT);

    // Divider
    C2D_DrawRectSolid(40, 32, 0.5f, TOP_SCREEN_WIDTH - 80, 1, CLR_DIVIDER);

    // Battery indicator (top-right)
    if (app->ptmu_ok) {
        float bx = TOP_SCREEN_WIDTH - 30.0f, by = 10.0f;
        float bw = 20.0f, bh = 10.0f;
        // Outline
        C2D_DrawRectSolid(bx, by, 0.8f, bw, bh, CLR_TEXT_WHITE);
        C2D_DrawRectSolid(bx + 1, by + 1, 0.8f, bw - 2, bh - 2, CLR_BG_DARK);
        // Nub
        C2D_DrawRectSolid(bx + bw, by + 3, 0.8f, 2, 4, CLR_TEXT_WHITE);
        // Fill (0-5 levels)
        float fill_w = (bw - 4) * app->battery_level / 5.0f;
        u32 fill_clr = app->battery_charging ? C2D_Color32(0x4C, 0xAF, 0x50, 0xFF)
                      : (app->battery_level <= 1 ? C2D_Color32(0xE5, 0x3E, 0x3E, 0xFF)
                      : CLR_TEXT_WHITE);
        if (fill_w > 0)
            C2D_DrawRectSolid(bx + 2, by + 2, 0.9f, fill_w, bh - 4, fill_clr);
    }

    C2D_TextBufClear(app->dynamic_buf);

    // Loading overlay
    if (app->loading) {
        C2D_Text ltxt;
        C2D_TextParse(&ltxt, app->dynamic_buf, "Opening book...");
        C2D_TextOptimize(&ltxt);
        C2D_DrawText(&ltxt, C2D_WithColor | C2D_AlignCenter,
                     TOP_SCREEN_WIDTH / 2.0f, 100.0f, 0.5f,
                     0.5f, 0.5f, CLR_TEXT_WHITE);
    }

    switch (app->current_screen) {
        case SCREEN_LIBRARY:
            library_draw_top(&app->library, app->dynamic_buf);
            break;

        case SCREEN_READER:
            reader_draw_top(&app->reader, app->dynamic_buf);
            break;

        case SCREEN_TRANSFER: {
            C2D_Text text;
            C2D_TextParse(&text, app->dynamic_buf,
                "Transfer Mode\n\n"
                "Open this URL in your browser\n"
                "to upload EPUB files:");
            C2D_TextOptimize(&text);
            C2D_DrawText(&text, C2D_WithColor | C2D_AlignCenter,
                         TOP_SCREEN_WIDTH / 2.0f, 50.0f, 0.5f,
                         0.45f, 0.45f, CLR_TEXT_WHITE);

            // Show URL
            char url[128];
            snprintf(url, sizeof(url), "http://%s:%d",
                     app->httpd.ip_str, app->httpd.port);
            C2D_TextParse(&text, app->dynamic_buf, url);
            C2D_TextOptimize(&text);
            C2D_DrawText(&text, C2D_WithColor | C2D_AlignCenter,
                         TOP_SCREEN_WIDTH / 2.0f, 120.0f, 0.5f,
                         0.6f, 0.6f, CLR_ACCENT);

            // Status
            C2D_TextParse(&text, app->dynamic_buf, app->httpd.status_msg);
            C2D_TextOptimize(&text);
            C2D_DrawText(&text, C2D_WithColor | C2D_AlignCenter,
                         TOP_SCREEN_WIDTH / 2.0f, 160.0f, 0.5f,
                         0.4f, 0.4f, CLR_TEXT_WHITE);
            break;
        }
    }

    // Error message overlay
    if (app->error_timer > 0) {
        app->error_timer--;
        C2D_Text err;
        C2D_TextParse(&err, app->dynamic_buf, app->error_msg);
        C2D_TextOptimize(&err);
        C2D_DrawRectSolid(0, 200, 0.9f, TOP_SCREEN_WIDTH, 20,
                          C2D_Color32(0xCC, 0x33, 0x33, 0xFF));
        C2D_DrawText(&err, C2D_WithColor | C2D_AlignCenter,
                     TOP_SCREEN_WIDTH / 2.0f, 202.0f, 1.0f,
                     0.4f, 0.4f, CLR_TEXT_WHITE);
    }

    // Help text at bottom
    C2D_DrawText(&app->help_text,
        C2D_WithColor | C2D_AlignCenter,
        TOP_SCREEN_WIDTH / 2.0f, 222.0f, 0.5f,
        0.35f, 0.35f, CLR_TEXT_WHITE);
}

void app_draw_bottom(AppState* app) {
    C2D_TextBufClear(app->dynamic_buf);

    if (app->loading) {
        C2D_Text ltxt;
        C2D_TextParse(&ltxt, app->dynamic_buf, "Opening...");
        C2D_TextOptimize(&ltxt);
        C2D_DrawText(&ltxt, C2D_WithColor | C2D_AlignCenter,
                     BOT_SCREEN_WIDTH / 2.0f, 110.0f, 0.5f,
                     0.5f, 0.5f, CLR_TEXT_BLACK);
        return;
    }

    switch (app->current_screen) {
        case SCREEN_LIBRARY:
            library_draw_bottom(&app->library, app->dynamic_buf);
            break;

        case SCREEN_READER:
            reader_draw_bottom(&app->reader);
            break;

        case SCREEN_TRANSFER: {
            C2D_DrawRectSolid(0, 0, 0.5f, BOT_SCREEN_WIDTH, 24, CLR_ACCENT);
            C2D_Text header;
            C2D_TextParse(&header, app->dynamic_buf, "File Transfer");
            C2D_TextOptimize(&header);
            C2D_DrawText(&header, C2D_WithColor, 8.0f, 4.0f, 0.5f,
                         0.5f, 0.5f, CLR_TEXT_WHITE);

            C2D_Text msg;
            char url[256];
            snprintf(url, sizeof(url),
                "Open in your browser:\n\n"
                "http://%s:%d\n\n"
                "%s\n\n"
                "Press B to go back.",
                app->httpd.ip_str, app->httpd.port,
                app->httpd.status_msg);
            C2D_TextParse(&msg, app->dynamic_buf, url);
            C2D_TextOptimize(&msg);
            C2D_DrawText(&msg, C2D_WithColor | C2D_AlignCenter,
                         BOT_SCREEN_WIDTH / 2.0f, 50.0f, 0.5f,
                         0.45f, 0.45f, CLR_TEXT_BLACK);
            break;
        }
    }

    // Top screen off indicator (small dot bottom-right)
    if (app->top_screen_off) {
        C2D_DrawRectSolid(BOT_SCREEN_WIDTH - 8, BOT_SCREEN_HEIGHT - 8,
                          0.9f, 6, 6, CLR_ACCENT);
    }
}

void app_cleanup(AppState* app) {
    aptUnhook(&app->apt_hook);
    save_reader_progress(app);
    reader_close(&app->reader);
    httpd_shutdown(&app->httpd);
    C2D_TextBufDelete(app->dynamic_buf);
    C2D_TextBufDelete(app->static_buf);
    if (app->ptmu_ok) ptmuExit();
    if (app->gsplcd_ok) {
        if (app->top_screen_off)
            GSPLCD_PowerOnBacklight(GSPLCD_SCREEN_TOP);
        gspLcdExit();
    }
}
