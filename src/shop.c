
#include "raylib.h"
#include "rlgl.h" // RLGL_IMPLEMENTATION is not needed and causes linkage error!
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
#define HT_IMPLEMENTATION
#include "ht.h"
#define CSV_SQL 
#define CSV_IMPLEMENTATION
#include "csv.h"

#include "shop.h"

#include "assets.c"
#include "shaders.c"
#include "home.c"
#include "display.c"
#include "admin.c"
#include "web_clipboard.c"

int main(int argc, char* argv[]) {
    // Global setup, and rlImGui
	int screen_width = 1280;
	int screen_height = 800;
	SetConfigFlags(FLAG_MSAA_4X_HINT | FLAG_VSYNC_HINT | FLAG_WINDOW_RESIZABLE);
	InitWindow(screen_width, screen_height, "shop");
	SetTargetFPS(144);
    SetExitKey(KEY_NULL);
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
            if (shop.admin.active) {
                shop.paused = true;
            }
        }
        
        // UPDATE
        if (!shop.paused) {
            update_shop(&shop);
        }

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

    sqlite3_close(shop.admin.db);
    arena_free(&shop.admin.current_ed->alloc);
    rlImGuiShutdown();
	CloseWindow();
}

void strip_file_name(char *path) {
    char *name = strrchr(path, '/');
    name = name ? name + 1 : path;
    memmove(path, name, strlen(name) + 1);
    char *dot = strrchr(path, '.');
    if (dot) {
        *dot = '\0';
    }
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
        (window_w - draw_w) * 0.5, (window_h - draw_h) * 0.5, 
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
    if (shop->paused) {
        DrawRectangle(0,0, GetScreenWidth(), GetScreenHeight(),
            Fade(BLACK, 0.60)
        );
    }

    rlImGuiBegin();
#ifdef PLATFORM_WEB
    web_clipboard_flush();
#endif
    if (shop->admin.active) {
        admin_panel(&shop->admin);
    } else {
        shop->paused = false;
    }
    rlImGuiEnd();
}

void screen_swap(Shop* shop, Screen target) {
    if (shop->transitioning) return;
    shop->transitioning = true;
    shop->fading_out = true;
    shop->transition_target = target;
    shop->transition_alpha = 0.0;
}

Vector2 mouse_pos_in_shop(Shop* shop) {
    Vector2 mouse = GetMousePosition();
    float window_w = (float)GetScreenWidth();
    float window_h = (float)GetScreenHeight();
    float target_w = (float)shop->render_target.texture.width;
    float target_h = (float)shop->render_target.texture.height;
    float scale = fminf(window_w/target_w, window_h/target_h);
    float draw_w = target_w * scale;
    float draw_h = target_h * scale;
    float offset_x = (window_w - draw_w) * 0.5;
    float offset_y = (window_h - draw_h) * 0.5;
    return (Vector2) {
        (mouse.x - offset_x) / scale,
        (mouse.y - offset_y) / scale
    };
}

void update_carousel(float* scroll, float* target, int count, float spacing, bool active) {
    if (active) {
        float wheel = GetMouseWheelMove();
        *target -= wheel * spacing;
        if (IsKeyPressed(KEY_RIGHT)) {
            *target += spacing;
        }
        if (IsKeyPressed(KEY_LEFT)) {
            *target -= spacing;
        }
    }
    float max_scroll = fmaxf(0.0, (count-1) * spacing);
    *target = Clamp(*target, 0.0, max_scroll);
    *scroll = Lerp(*scroll, *target, 1.0 - powf(0.001, GetFrameTime()));
}

void draw_product_information_cube(Shop* shop, Item item, Vector3 pos, float alpha) {
    float width = 5.0;
    float height = 1.8;
    float depth = 0.12;
    pos.y += 2.5;

    char price[64];
    snprintf(price, sizeof(price), "$%.2f", item.price);
    char stock[64];
    snprintf(stock, sizeof(stock), "%d in stock", item.stock);

    Vector2 mouse = mouse_pos_in_shop(shop);
    float screen_w = shop->render_target.texture.width;
    float screen_h = shop->render_target.texture.height;
    float mouse_x = (mouse.x / screen_w - 0.5) * 2.0;
    float mouse_y = (mouse.y / screen_h - 0.5) * 2.0;
    mouse_x = Clamp(mouse_x, -1.0, 1.0);
    mouse_y = Clamp(mouse_y, -1.0, 1.0);
    float yaw   = mouse_x * 8.0;
    float pitch = mouse_y * 5.0;
    rlPushMatrix();
        rlTranslatef(pos.x, pos.y, pos.z);
        rlRotatef(yaw,   0.0, 1.0, 0.0);
        rlRotatef(pitch, 1.0, 0.0, 0.0);
        // THE PRODUCT INFORMATION CUBE
        DrawCube(
            (Vector3){ 0.0, 0.0, 0.0 },
            width,
            height,
            depth,
            Fade(RAYWHITE, alpha)
        );
        DrawCubeWires(
            (Vector3){ 0.0, 0.0, 0.0 },
            width,
            height,
            depth,
            Fade(BLACK, alpha)
        );

        // only the *most carefully curated* of magic of numbers
        float text_z = depth * 0.5 + 1.0;
        DrawTextCentered3D(
            SHOP_FONT,
            item.display,
            (Vector3){ 0.0, 0.55, text_z },
            0.32, 0.015,
            Fade(BLACK, alpha)
        );
        DrawTextWordWrapped3D(
            SHOP_FONT,
            item.description,
            (Vector3){ 0.0, 0.10, text_z },
            4.2, 0.16, 0.01, 0.08,
            Fade(DARKGRAY, alpha)
        );
        DrawTextCentered3D(
            SHOP_FONT,
            price,
            (Vector3){ -1.3, -0.50, text_z },
            0.22, 0.01,
            Fade(BLACK, alpha)
        );
        DrawTextCentered3D(
            SHOP_FONT,
            stock,
            (Vector3){ 1.3, -0.50, text_z },
            0.18, 0.008,
            Fade(DARKGRAY, alpha)
        );
    rlPopMatrix();
}

void draw_carousel(Shop* shop, Item_List* items, float scroll, float spacing, float y) {
    Camera3D camera = shop->camera;
    camera.position.x = scroll;
    camera.target.x = scroll;

    BeginMode3D(camera);
    for (size_t i = 0; i < items->count; i++) {
        Item item = items->items[i];
        Item_Resource* resource = get_item_resource(shop, item); 
        if (!resource) {
            continue;
        }
        float item_x = i * spacing;
        float center_dist = fabsf(item_x - scroll);
        float focus = 1.0 - Clamp(
            center_dist / spacing,
            0.0, 1.0
        );
        float focus_scale = Lerp(0.65, 1.15, focus);
        Vector3 pos = {
            item_x,
            y + Lerp(-0.25, 0.0, focus),
            Lerp(1.5, 0.0, focus)
        };
        if (resource->is_model) {
            float spin = GetTime() * 25.0 + i * 47.0;
            DrawModelEx(
                resource->model,
                pos,
                (Vector3){ 0.2, 1.0, 0.2 },
                spin,
                (Vector3) {
                    resource->scale * focus_scale,
                    resource->scale * focus_scale,
                    resource->scale * focus_scale
                },
                WHITE
            );
        } else {
            Rectangle source = {
                0.0,0.0,
                (float)resource->texture.width,(float)resource->texture.height
            };
            float scale = resource->scale * focus_scale;
            float size_y = ((float)resource->texture.height / (float)resource->texture.width);
            Vector2 size = {
                scale,
                scale * size_y
            };
            Vector2 origin = {
                size.x * 0.5,0
            };

            DrawBillboardPro(
                camera,
                resource->texture,
                source,
                pos,
                (Vector3){ 0.0, 1.0, 0.0},
                size,
                origin,
                0.0,
                WHITE
            );
        }
    }

    if (items->count > 0) {
        int index = (int)roundf(scroll / spacing);
        if (index < 0) {
            index = 0;
        }
        if (index >= (int)items->count) {
            index = (int)items->count - 1;
        }
        float item_x = index * spacing;
        float dist = fabsf(scroll - item_x);
        float alpha = 1.0 - Clamp(dist / (spacing * 0.35), 0.0, 1.0);

        draw_product_information_cube(shop, items->items[index], (Vector3){ scroll, y, -1.0 }, alpha);
    }

    EndMode3D();
}

// NOTE!!
// the sql must be some form of `"SELECT " ITEM_COLUMNS " FROM items "`
// or else BAD THINGS WILL HAPPEN!!!
Item_List query_items(Shop* shop, char* sql) {
    Item_List list = {0};
    SQL_Result result = sql_run(&shop->admin, sql);
    if (result.error) {
        printf("%s\n", result.error);
        return list;
    }

    for (int y=0; y<result.height; y++) {
        Item item = {0};
        item.id = atoi(CELL(&result, 0, y));
        snprintf(item.name, sizeof(item.name), "%s", 
            CELL(&result, ITEM_NAME_COLUMN, y)
        );
        snprintf(item.description, sizeof(item.description), "%s", 
            CELL(&result, ITEM_DESC_COLUMN, y)
        );
        item.price = strtof(CELL(&result, ITEM_PRICE_COLUMN, y), NULL);
        item.stock = atoi(CELL(&result, ITEM_STOCK_COLUMN, y));
        snprintf(item.category, sizeof(item.category), "%s", 
            CELL(&result, ITEM_CATEGORY_COLUMN, y)
        );
        snprintf(item.display, sizeof(item.category), "%s", 
            CELL(&result, ITEM_DISPLAY_COLUMN, y)
        );

        arena_da_append(&list.alloc, &list, item);
    }
    return list;
}

void reset_item_list(Item_List* list) {
    arena_reset(&list->alloc);
    list->items = NULL;
    list->count = 0;
    list->capacity = 0;
}

bool init_shop(Shop *shop) {
    // Admin UI Setup
    Admin_Panel* admin = &shop->admin;
    Text_Editor* ed = (Text_Editor*)malloc(sizeof(Text_Editor));
    init_text_ed(ed);
    init_admin_panel(admin, ed);

    // Database setup
    int rc = sqlite3_open(":memory:", &admin->db); // NO PERSISTANT DB FOR NOW!!
    if (rc != SQLITE_OK) {
        printf("sqlite open failed: `%s`\n", sqlite3_errmsg(admin->db));
        return false;
    }
    // load `csv`s
    char path[] = "assets/csv/items.csv"; 
    SQL_Result csv = admin_load_csv(admin, path);
    if (csv.error) {
        printf("admin_load_csv failed: `%s`\n", csv.error);
        return false;
    }
    schema_list_refresh(admin);

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
    shop->camera.position = (Vector3){ 0.0, 0.0, 7.0 };
    shop->camera.target   = (Vector3){ 0.0, 0.0, 0.0 };
    shop->camera.up       = (Vector3){ 0.0, 1.0, 0.0 };
    shop->camera.fovy     = 45.0;
    shop->camera.projection = CAMERA_PERSPECTIVE;

    // actual Shop setup
    shop->screen = LOAD_SCREEN;
    init_home_menu_buttons(shop);
    // inline SQL is CRAZY
    // it looks terrible, bust trust me, inline GCC assembly is far worse!
    shop->featured = query_items(shop,
        "SELECT " ITEM_COLUMNS " FROM items "
        "WHERE name IN ("
            "'wooden_chair', "
            "'caveman_chair', "
            "'the_batmobile', "
            "'lawn_chair', "
            "'refrigerator', "
            "'kingly_throne'"
        ") "
        "ORDER BY CASE name "
            "WHEN 'wooden_chair'   THEN 0 "
            "WHEN 'caveman_chair' THEN 1 "
            "WHEN 'the_batmobile' THEN 2 "
            "WHEN 'lawn_chair'    THEN 3 "
            "WHEN 'refrigerator'  THEN 4 "
            "WHEN 'kingly_throne' THEN 5 "
        "END;"
    );
    shop->paused = false;

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
        update_home(shop);
        break;

    case DISPLAY_SCREEN:
        update_display(shop);
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
        Rectangle source = { 0,0, (float)LOGO.width, (float)LOGO.height };
        Rectangle dest = { 
            (screen_w - draw_w)/2, (logo_area_h - draw_h)/2, 
            draw_w, draw_h 
        };
        DrawTexturePro(LOGO, source, dest, (Vector2){0,0}, 0.0, WHITE);

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
        if (elapsed > 5.0 || IsKeyPressed(KEY_ESCAPE)) {
            screen_swap(shop, HOME_SCREEN);
        }
    }break;

    case HOME_SCREEN:
        ClearBackground(SHOP_BG);
        draw_home(shop);
        break;

    case DISPLAY_SCREEN:
        ClearBackground(SHOP_BG);
        draw_display(shop);
        break;
    }

    if (shop->transition_alpha > 0.0) {
        DrawRectangle(0,0, screen_w, screen_h, Fade(BLACK, shop->transition_alpha));
    }
}

