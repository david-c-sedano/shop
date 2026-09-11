
#define DISPLAY_Y 2.5
#include "shop.h"
#include "shop_db.h"

void init_shop_items(Shop* shop) {
    // 1. Camera setup
    shop->camera.position = (Vector3){ 0.0f, DISPLAY_Y, 7.0f };
    shop->camera.target = (Vector3){ 0.0f, DISPLAY_Y, 0.0f };
    shop->camera.up = (Vector3){ 0.0f, 1.0f, 0.0f };
    shop->camera.fovy = 45.0f;
    shop->camera.projection = CAMERA_PERSPECTIVE;

    shop->items = (Item*)malloc(sizeof(Item)*4);
    shop->item_count = 4;
    shop->item_cap = 0;

    if (!shop_db_load_products(shop)) {
        printf("[ERROR] Failed to load shop products from database.\n");
    }
}

void update_item_display(Shop* shop) {
    float wheel = GetMouseWheelMove();
    shop->scroll_target -= wheel * 2.0;
    if (IsKeyPressed(KEY_RIGHT)) {
        shop->scroll_target += 4.0;
    }
    if (IsKeyPressed(KEY_LEFT)) {
        shop->scroll_target -= 4.0;
    }
    float max_scroll = (shop->item_count-1) * 4.0;
    shop->scroll_target = Clamp(shop->scroll_target, 0.0, max_scroll);
    float dt = GetFrameTime();
    shop->scroll = Lerp(shop->scroll, shop->scroll_target, 1.0-powf(0.001, GetFrameTime()));
}
void draw_item_name_box(const char* name, Vector2 screen_anchor) {
    int font_size = 20;
    int padding_x = 16;
    int padding_y = 8;

    int text_w = MeasureText(name, font_size);
    int box_w = text_w + (padding_x * 2);
    int box_h = font_size + (padding_y * 2);

    // Center the box horizontally above the 3D anchor
    Rectangle box_rect = {
        screen_anchor.x - (box_w * 1.0f),
        screen_anchor.y - box_h - 0.0f,
        (float)box_w,
        (float)box_h
    };

    // Background and border
    DrawRectangleRec(box_rect, SHOP_WHITE);
    DrawRectangleLinesEx(box_rect, 2.0f, SHOP_ORANGE);

    // Centering box
    DrawText(name, (int)(box_rect.x + padding_x), (int)(box_rect.y + padding_y), font_size, SHOP_INK);
}
void draw_item_price_box(const char* price, Vector2 screen_anchor) {
    int font_size = 20;
    int padding_x = 16;
    int padding_y = 8;

    int text_w = MeasureText(price, font_size);
    int box_w = text_w + (padding_x * 2);
    int box_h = font_size + (padding_y * 2);

    // Center the box horizontally above the 3D anchor
    Rectangle box_rect = {
        screen_anchor.x - (box_w * 1.0f),
        screen_anchor.y - box_h - 0.0f,
        (float)box_w,
        (float)box_h
    };

    // Background & border using your palette colors
    DrawRectangleRec(box_rect, SHOP_WHITE);
    DrawRectangleLinesEx(box_rect, 2.0f, SHOP_ORANGE);

    // Centered text
    DrawText(price, (int)(box_rect.x + padding_x), (int)(box_rect.y + padding_y), font_size, SHOP_INK);
}
void draw_item_display(Shop* shop) {
    
    Camera3D camera = shop->camera;
    camera.position.x = shop->scroll;
    camera.target.x = shop->scroll;

    BeginMode3D(camera);
    float spacing = 4.0;

    for (int i=0; i<shop->item_count; i++) {
        Item* item = &shop->items[i];
        float item_x = i * spacing;
        float center_dist = fabsf(item_x - shop->scroll);
        float focus = 1.0 - Clamp(center_dist / spacing, 0.0, 1.0);
        float scale = Lerp(0.65, 1.15, focus);

        Vector3 pos = { 
            item_x, 
            DISPLAY_Y + Lerp(-0.25, 0.0, focus), 
            Lerp(1.5, 0.0, focus) 
        };
        Vector3 size = Vector3Scale(item->size, scale);
        DrawCubeV(pos, size, WHITE);
        DrawCubeWiresV(pos, size, BLACK);
    }
    EndMode3D();
    

    for (int i = 0; i < shop->item_count; i++) {
        Item* item = &shop->items[i];
        float item_x = (float)i * spacing;

        // Position anchor above the top face of the cube
        Vector3 top_anchor = { item_x, DISPLAY_Y + (item->size.y * 0.5f), 0.0f };
        Vector3 bottom_anchor = { item_x, DISPLAY_Y - (item->size.y * 0.5f), 0.0f };
        Vector2 screen_top = GetWorldToScreen(top_anchor, camera);
        Vector2 screen_bottom = GetWorldToScreen(bottom_anchor, camera);

        // Optional culling: skip drawing if off the internal 1000x800 render texture
        if (screen_top.x < 200.0f || screen_top.x >(float)shop->render_target.texture.width + 100.0f) {
            continue;
        }

        // Draw name and price boxes centered above/below the cube can be later changed to 1 function 
        draw_item_name_box(item->name, screen_top);
        draw_item_price_box(TextFormat("$%.2f", item->price), screen_bottom);
    }
}
