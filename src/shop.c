
#include "raylib.h"
#include "raymath.h"
#include "cimgui.h"
#include "rlImGui.h"
#include "sqlite3.h"

#ifdef PLATFORM_WEB
#include "emscripten.h"
#endif

#include "stdio.h"
#include "stdlib.h"
#include "string.h"
#include "float.h"

#define ARENA_IMPLEMENTATION
#include "arena.h"

#include "admin.c"
#include "web_clipboard.c"

int main(int argc, char* argv[]) {
	int screen_width = 1280;
	int screen_height = 800;

	SetConfigFlags(FLAG_MSAA_4X_HINT | FLAG_VSYNC_HINT | FLAG_WINDOW_RESIZABLE);
	InitWindow(screen_width, screen_height, "shop");
	SetTargetFPS(144);
	rlImGuiSetup(true);

#ifdef PLATFORM_WEB
    ImGuiPlatformIO* pio = ImGui_GetPlatformIO();
    pio->Platform_GetClipboardTextFn = web_sync_clipboard_is_fricked;
    pio->Platform_SetClipboardTextFn = web_set_clipboard;
    install_paste_hook();
#endif

    bool demo_window_open = false;
    Text_Editor ed = {0};
    init_text_ed(&ed);
    Admin_Panel admin = {0};
    admin_panel_init(&admin, &ed);
    // defer arena_free(&ed.alloc);
    // NO DEFER? SCREW THIS GOOFY *** LANGUAGE

    int rc = sqlite3_open(":memory:", &admin.db); // NO PERSISTANT DB FOR NOW!!
    if (rc != SQLITE_OK) {
        printf("sqlite open failed: `%s`\n", sqlite3_errmsg(admin.db));
        return 1;
    }

	while (!WindowShouldClose()) {
		BeginDrawing();
		ClearBackground(DARKGRAY);

		rlImGuiBegin();

#ifdef PLATFORM_WEB
        web_clipboard_flush();
#endif

        if (demo_window_open) {
		    ImGui_ShowDemoWindow(&demo_window_open);
        }

        admin_panel(&admin);

		rlImGuiEnd();
		EndDrawing();
	}

    sql_result_free(&admin.prev_result);
    sqlite3_close(admin.db);
    arena_free(&ed.alloc);
    rlImGuiShutdown();
	CloseWindow();
}
