
#define DISPLAY_Y 2.5

void init_shop_items(Shop* shop) {
    shop->camera.position = (Vector3) { 0.0, DISPLAY_Y, 7.0 };
    shop->camera.target = (Vector3) { 0.0, DISPLAY_Y, 0.0 };
    shop->camera.up = (Vector3) { 0.0, 1.0, 0.0 };
    shop->camera.fovy = 45.0;
    shop->camera.projection = CAMERA_PERSPECTIVE;

    shop->items = (Item*)malloc(sizeof(Item)*4);
    shop->items[0] = (Item){
        .size = { 1.5, 2.0, 1.0 },
        .scale = 1.0,
    };
    shop->items[1] = (Item){
        .size = { 2.0, 1.0, 1.0 },
        .scale = 1.0,
    };
    shop->items[2] = (Item){
        .size = { 1.0, 2.5, 1.0 },
        .scale = 1.0,
    };
    shop->items[3] = (Item){
        .size = { 1.8, 1.8, 1.8 },
        .scale = 1.0,
    };
    shop->item_count = 4;
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
}
