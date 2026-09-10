
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

#include "shop.h"
#include "colors.c"
#include "shaders.c"
#include "items.c"
#include "admin.c"
#include "web_clipboard.c"

int main(int argc, char* argv[]) {
    // Global setup, and rlImGui
	int screen_width = 1280;
	int screen_height = 800;
	SetConfigFlags(FLAG_MSAA_4X_HINT | FLAG_VSYNC_HINT | FLAG_WINDOW_RESIZABLE);
	InitWindow(screen_width, screen_height, "shop");
	SetTargetFPS(144);
	rlImGuiSetup(true);

#ifdef PLATFORM_WEB
    // clipboard hack for web
    ImGuiPlatformIO* pio = ImGui_GetPlatformIO();
    pio->Platform_GetClipboardTextFn = web_sync_clipboard_is_fricked;
    pio->Platform_SetClipboardTextFn = web_set_clipboard;
    install_paste_hook();
#endif

    // Admin UI Setup
    Text_Editor ed = {0};
    init_text_ed(&ed);
    Admin_Panel admin = {0};
    admin_panel_init(&admin, &ed);

    // Database setup
    int rc = sqlite3_open(":memory:", &admin.db); // NO PERSISTANT DB FOR NOW!!
    if (rc != SQLITE_OK) {
        printf("sqlite open failed: `%s`\n", sqlite3_errmsg(admin.db));
        return 1;
    }

    // Renderer setup
    int render_width = 1000;
    int render_height = 800;
    RenderTexture2D target = LoadRenderTexture(render_width, render_height);
    Shader crt = LoadShaderFromMemory(VERTEX, FRAGMENT);
    int resolution_loc = GetShaderLocation(crt, "resolution");
    int time_loc = GetShaderLocation(crt, "time");
    Vector2 resolution = {(float)target.texture.width, (float)target.texture.height};
    SetShaderValue(crt, resolution_loc, &resolution, SHADER_UNIFORM_VEC2);

    // actual Shop setup
    Shop shop;
    init_shop_items(&shop);

	while (!WindowShouldClose()) {
        // INPUT
        bool admin_shortcut = (IsKeyDown(KEY_LEFT_CONTROL) || IsKeyDown(KEY_RIGHT_CONTROL)) && IsKeyPressed(KEY_A);
        if (admin_shortcut) {
            admin.active = !admin.active;
        }
        
        // UPDATE
        update_shop(&shop);

        // DRAW
        BeginTextureMode(target);
        ClearBackground(SHOP_BG);
            draw_shop_display(&shop);
        EndTextureMode();

		BeginDrawing();
            ClearBackground(BLACK);

            float time = (float)GetTime();
            SetShaderValue(crt, time_loc, &time, SHADER_UNIFORM_FLOAT);
            shop_render_pass(target, crt);

            ui_render_pass(&admin);
		EndDrawing();
	}

    sql_result_free(&admin.prev_result);
    sqlite3_close(admin.db);
    arena_free(&ed.alloc);
    rlImGuiShutdown();
	CloseWindow();
}

void shop_render_pass(RenderTexture2D target, Shader shader) {
    // DUMP!!!
    float window_w = (float)GetScreenWidth();
    float window_h = (float)GetScreenHeight();
    float target_w = (float)target.texture.width;
    float target_h = (float)target.texture.height;
    float scale = fminf(window_w / target_w, window_h / target_h);
    float draw_w = target_w * scale;
    float draw_h = target_h * scale;

    BeginShaderMode(shader);
    Rectangle source = { 0,0,target_w,-target_h };
    Rectangle dest = { 
        (window_w - draw_w) * 0.5f, (window_h - draw_h) * 0.5f, 
        draw_w, draw_h
    };
    DrawTexturePro(
        target.texture,
        source,
        dest,
        (Vector2){0, 0},
        0.0f,
        WHITE
    );
    EndShaderMode();
}

void ui_render_pass(Admin_Panel* admin) {
    rlImGuiBegin();
#ifdef PLATFORM_WEB
    web_clipboard_flush();
#endif
    if (admin->active) {
        admin_panel(admin);
    }
    rlImGuiEnd();
}
