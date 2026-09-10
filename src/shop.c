
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
#include "item_display.c"
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

    Shop shop = {0};
    bool ok = init_shop(&shop);
    if (!ok) {
        return 1;
    }

	while (!WindowShouldClose()) {
        // INPUT
        bool admin_shortcut = (IsKeyDown(KEY_LEFT_CONTROL) || IsKeyDown(KEY_RIGHT_CONTROL)) && IsKeyPressed(KEY_A);
        if (admin_shortcut) {
            shop.admin.active = !shop.admin.active;
        }
        
        // UPDATE
        update_shop(&shop);

        // DRAW
        BeginTextureMode(shop.texture);
        ClearBackground(SHOP_BG);
            draw_shop(&shop);
        EndTextureMode();

		BeginDrawing();
            ClearBackground(BLACK);
            shop_render_pass(&shop);
            ui_render_pass(&shop);
		EndDrawing();
	}

    sql_result_free(&shop.admin.prev_result);
    sqlite3_close(shop.admin.db);
    arena_free(&shop.admin.current_ed->alloc);
    rlImGuiShutdown();
	CloseWindow();
}

void shop_render_pass(Shop* shop) {
    int time_loc = shop->time_loc;
    float time = (float)GetTime();
    RenderTexture2D target = shop->texture;
    Shader shader = shop->shader;
    SetShaderValue(shader, time_loc, &time, SHADER_UNIFORM_FLOAT);

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

void ui_render_pass(Shop* shop) {
    rlImGuiBegin();
#ifdef PLATFORM_WEB
    web_clipboard_flush();
#endif
    if (shop->admin.active) {
        admin_panel(&shop->admin);
    }
    rlImGuiEnd();
}

bool init_shop(Shop *shop) {
    // Admin UI Setup
    Admin_Panel* admin = &shop->admin;
    Text_Editor* ed = (Text_Editor*)malloc(sizeof(Text_Editor));
    init_text_ed(ed);
    admin_panel_init(admin, ed);

    // Database setup
    int rc = sqlite3_open(":memory:", &admin->db); // NO PERSISTANT DB FOR NOW!!
    if (rc != SQLITE_OK) {
        printf("sqlite open failed: `%s`\n", sqlite3_errmsg(admin->db));
        return false;
    }

    // Renderer setup
    int render_width = 1000;
    int render_height = 800;
    RenderTexture2D target = LoadRenderTexture(render_width, render_height);
    shop->shader = LoadShaderFromMemory(VERTEX, FRAGMENT);
    int resolution_loc = GetShaderLocation(shop->shader, "resolution");
    shop->time_loc = GetShaderLocation(shop->shader, "time");
    Vector2 resolution = {(float)target.texture.width, (float)target.texture.height};
    SetShaderValue(shop->shader, resolution_loc, &resolution, SHADER_UNIFORM_VEC2);
    shop->texture = target;

    // actual Shop setup
    init_shop_items(shop);
    shop->screen = DISPLAY_SCREEN;
    return true;
}

void update_shop(Shop *shop) {
    switch (shop->screen) {
    case LOAD_SCREEN:
        break;

    case HOME_SCREEN:
        break;

    case DISPLAY_SCREEN:
        update_item_display(shop);
        break;
    }
}

void draw_shop(Shop *shop) {
    switch (shop->screen) {
    case LOAD_SCREEN:
        break;

    case HOME_SCREEN:
        break;

    case DISPLAY_SCREEN:
        draw_item_display(shop);
        break;
    }
}


