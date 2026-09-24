
#define DISPLAY_Y -1.2
#define DISPLAY_SPACING 2.0

void init_shop_items(Shop* shop) {
    shop->camera.position = (Vector3){ 0.0f, DISPLAY_Y, 7.0f };
    shop->camera.target = (Vector3){ 0.0f, DISPLAY_Y, 0.0f };
    shop->camera.up = (Vector3){ 0.0f, 1.0f, 0.0f };
    shop->camera.fovy = 45.0f;
    shop->camera.projection = CAMERA_PERSPECTIVE;
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

static Rectangle back_button_bounds(Shop* shop) {
    float screen_h = shop->render_target.texture.height;
    return (Rectangle){
        24.0, screen_h - 100.0, 120.0, 80.0
    };
}

void update_display(Shop* shop) {
    update_carousel(
        &shop->scroll,
        &shop->scroll_target,
        shop->display.count,
        DISPLAY_SPACING,
        true
    );
    Rectangle back = back_button_bounds(shop);
    Vector2 mouse = mouse_pos_in_shop(shop);
    if (CheckCollisionPointRec(mouse, back) && IsMouseButtonReleased(MOUSE_BUTTON_LEFT)) {
        screen_swap(shop, HOME_SCREEN);
    }
}

void draw_back_button(Shop* shop) {
    Rectangle back = back_button_bounds(shop);
    Vector2 mouse = mouse_pos_in_shop(shop);
    bool hovered = CheckCollisionPointRec(mouse, back);
    
    float darken = IsMouseButtonDown(MOUSE_BUTTON_LEFT) ? -0.8 : -0.5;
    DrawRectangleRounded(back, 0.2, 8, 
        hovered ? ColorBrightness(SHOP_GREEN, darken) : SHOP_GREEN
    );

    const char* label = "< HOME";
    int font_size = 20;
    int label_w = MeasureText(label, font_size);
    DrawText(
        label,
        back.x + (back.width - label_w) * 0.5f,
        back.y + (back.height - font_size) * 0.5f,
        font_size,
        BLACK
    );
}

void draw_display(Shop* shop) {
    Camera3D camera = shop->camera;
    camera.position.x = shop->scroll;
    camera.target.x = shop->scroll;
    BeginMode3D(camera);

    draw_carousel(
        shop,
        &shop->display,
        shop->scroll,
        DISPLAY_SPACING,
        DISPLAY_Y
    );

    int index = (int)roundf(shop->scroll / DISPLAY_SPACING);
    if (index < 0) {
        index = 0;
    }
    if (index >= (int)shop->display.count) {
        index = (int)shop->display.count - 1;
    }
    float item_x = index * DISPLAY_SPACING;
    float dist = fabsf(shop->scroll - item_x);
    float alpha = 1.0 - Clamp(dist / (DISPLAY_SPACING * 0.35), 0.0, 1.0);
    if (shop->display.count > 0) {
        draw_product_information_cube(
            shop, shop->display.items[index], (Vector3){ shop->scroll, DISPLAY_Y, -1.0 }, alpha
        );
    }
    EndMode3D();

    draw_back_button(shop);
}
