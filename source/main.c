#include <3ds.h>
#include <citro2d.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

#include "app.h"

int main(int argc, char* argv[]) {
    (void)argc;
    (void)argv;

    // Initialize graphics
    gfxInitDefault();
    C3D_Init(C3D_DEFAULT_CMDBUF_SIZE);
    C2D_Init(C2D_DEFAULT_MAX_OBJECTS);
    C2D_Prepare();

    // Initialize app state (static to avoid stack overflow - struct is ~330KB)
    static AppState app;
    app_init(&app);

    // Main loop
    while (aptMainLoop()) {
        hidScanInput();
        u32 kDown = hidKeysDown();
        u32 kHeld = hidKeysHeld();
        touchPosition touch;
        hidTouchRead(&touch);

        if (kDown & KEY_START)
            break;

        app_update(&app, kDown, kHeld, &touch);

        // Always begin/end a frame so citro3d's GPU state stays consistent
        // across APT transitions (Home button). Only redraw when needed.
        C3D_FrameBegin(C3D_FRAME_SYNCDRAW);

        if (kDown || (kHeld & KEY_TOUCH) || app.needs_redraw) {
            app.needs_redraw = false;

            C2D_TargetClear(app.top, app.top_clear_color);
            C2D_SceneBegin(app.top);
            app_draw_top(&app);

            C2D_TargetClear(app.bottom, app.bottom_clear_color);
            C2D_SceneBegin(app.bottom);
            app_draw_bottom(&app);
        }

        C3D_FrameEnd(0);
    }

    // Cleanup
    app_cleanup(&app);
    C2D_Fini();
    C3D_Fini();
    gfxExit();

    return 0;
}
