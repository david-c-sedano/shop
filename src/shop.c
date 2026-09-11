
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
#include "resources.c"
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
    init_textures();
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
        BeginTextureMode(shop.render_target);
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
    RenderTexture2D target = shop->render_target;
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

void screen_swap(Shop* shop, Screen target) {
    if (shop->transitioning) return;
    shop->transitioning = true;
    shop->fading_out = true;
    shop->transition_target = target;
    shop->transition_alpha = 0.0f;
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
    shop->render_target = target;

    // actual Shop setup
    init_shop_items(shop);
    shop->screen = LOAD_SCREEN;
    return true;
}

void update_shop(Shop *shop) {
    float dt = GetFrameTime();
    if (shop->transitioning) {
        float speed = 1.5;
        if (shop->fading_out) {
            shop->transition_alpha += speed * dt;
            if (shop->transition_alpha >= 1.0) {
                shop->transition_alpha = 1.0;
                shop->screen = shop->transition_target;
                shop->fading_out = false;
            }
        } else {
            shop->transition_alpha -= speed * dt;
            if (shop->transition_alpha <= 0.0) {
                shop->transition_alpha = 0.0;
                shop->transitioning = false;
            }
        }
    }

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
    float screen_w = shop->render_target.texture.width;
    float screen_h = shop->render_target.texture.height;

    switch (shop->screen) {
    case LOAD_SCREEN: {
        ClearBackground((Color){ 230,230,230,255 });
        // calculate logo centering and draw 
        float logo_area_h = screen_h - 140.0;
        float scale = fminf(screen_w/(float)LOGO.width, logo_area_h/(float)LOGO.height);
        scale *= 0.8;
        float draw_w = LOGO.width * scale;
        float draw_h = LOGO.height * scale;
        Rectangle src = { 0,0, (float)LOGO.width, (float)LOGO.height };
        Rectangle dest = { 
            (screen_w - draw_w)/2, (logo_area_h - draw_h)/2, 
            draw_w, draw_h 
        };
        DrawTexturePro(LOGO, src, dest, (Vector2){0,0}, 0.0, WHITE);

        // goofy loading bar
        float bar_w = 800.0; 
        float bar_h = 30.0; 
        float bar_x = (screen_w - bar_w) * 0.5; 
        float bar_y = screen_h - 130.0; 
        float elapsed = (float)GetTime();
        // `static` is incredibly good for throwaway gags
        static float progress = 0.0;
        static float target = 0.0;
        if (elapsed < 4.0) {
            if (fabsf(progress - target) < 0.02) {
                target = (float)GetRandomValue(10, 90) / 100.0;
            }
        } else {
            target = 1.0;
        }
        progress = Lerp(progress, target, 3.0 * GetFrameTime());
        DrawRectangleRounded(
            (Rectangle){bar_x, bar_y, bar_w * progress, bar_h},
            1.0, 5, SHOP_GREEN
        );
        if (elapsed > 5.0) {
            screen_swap(shop, DISPLAY_SCREEN);
        }
    }break;

    case HOME_SCREEN:
        break;

    case DISPLAY_SCREEN:
        ClearBackground(SHOP_BG);
        draw_item_display(shop);
        break;
    }

    if (shop->transition_alpha > 0.0) {
        DrawRectangle(0,0, screen_w, screen_h, Fade(BLACK, shop->transition_alpha));
    }
}

