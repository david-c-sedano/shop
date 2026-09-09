
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

void shop_render_pass(RenderTexture2D target, Shader shader);
void ui_render_pass(Admin_Panel* admin);

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

    int render_width = 1000;
    int render_height = 800;
    RenderTexture2D target = LoadRenderTexture(render_width, render_height);
    Shader crt = LoadShader("./src/shop.vs", "./src/shop.fs");
    int resolution_loc = GetShaderLocation(crt, "resolution");
    Vector2 resolution = {(float)target.texture.width, (float)target.texture.height};
    SetShaderValue(crt, resolution_loc, &resolution, SHADER_UNIFORM_VEC2);

	while (!WindowShouldClose()) {
        // INPUT
        bool admin_shortcut = (IsKeyDown(KEY_LEFT_SUPER) || IsKeyDown(KEY_RIGHT_SUPER)) && IsKeyPressed(KEY_P);
        if (admin_shortcut) {
            admin.active = !admin.active;
        }

        BeginTextureMode(target);
        ClearBackground(DARKGRAY);
            const char* text = "deez nuts";
            DrawText(text, 100, screen_height/2, 120, GREEN);
        EndTextureMode();

		BeginDrawing();
            ClearBackground(BLACK);
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
