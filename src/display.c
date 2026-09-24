
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

    Vector2 yaw_pitch = mouse_yaw_pitch(shop);
    float yaw = yaw_pitch.x;
    float pitch = yaw_pitch.y;

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

void update_display(Shop* shop) {
    update_carousel(
        &shop->scroll,
        &shop->scroll_target,
        shop->display.count,
        DISPLAY_SPACING,
        true
    );
    search_bar_event(shop);
    back_button_event(shop);
}

void draw_display(Shop* shop) {
    // search bar
    draw_search_bar(shop);

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

    int index = carousel_focused_item_index(shop->display, shop->scroll, DISPLAY_SPACING);
    if (shop->display.count > 0) {
        draw_product_information_cube(
            shop, 
            shop->display.items[index], 
            (Vector3){ shop->scroll, DISPLAY_Y, -1.0 }, 
            carousel_item_alpha(shop->display, shop->scroll, DISPLAY_SPACING)
        );
    }
    EndMode3D();

    draw_back_button(shop);
}
