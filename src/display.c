
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

    char price[128];
    char original_price[128] = {0};
    bool on_sale = item_is_on_sale(item);

    if (on_sale) {
        snprintf(price, sizeof(price), "ON SALE: $%.2f", item.sale_price);
        // Show the original price and rounded percentage saved on all sale items.
        int percent_off = item.price > 0.0f
            ? (int)roundf((item.price - item.sale_price) * 100.0f / item.price)
            : 0;
        snprintf(original_price, sizeof(original_price),
                 "Was $%.2f  (%d%% OFF)", item.price, percent_off);
    } else {
        format_money(price, sizeof(price), item.price);
    }

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
            width,height,depth,
            Fade(RAYWHITE, alpha)
        );
        DrawCubeWires(
            (Vector3){ 0.0, 0.0, 0.0 },
            width,height,depth,
            Fade(BLACK, alpha)
        );

        // only the *most carefully curated* of magic of numbers
        float text_z = depth * 0.5 + 1.0;
        DrawTextCentered3D(
            SHOP_FONT,
            item.display,
            (Vector3){ 0.0, 0.40, text_z },
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
            (Vector3){ -1.3, on_sale ? -0.48 : -0.60, text_z },
            0.22, 0.01,
            Fade(BLACK, alpha)
        );
        if (on_sale) {
            DrawTextCentered3D(
                SHOP_FONT,
                original_price,
                (Vector3){ -1.3, -0.76, text_z },
                0.15, 0.008,
                Fade(DARKGRAY, alpha)
            );
        }
        DrawTextCentered3D(
            SHOP_FONT,
            stock,
            (Vector3){ 1.3, -0.60, text_z },
            0.18, 0.008,
            Fade(DARKGRAY, alpha)
        );
    rlPopMatrix();
}

// Yeah, I'm just pissing around at this point
void draw_no_items(Shop* shop, Camera camera) {
    float image_width = 3.0;
    float image_height = image_width * ((float)MEGAMIND.height / (float)MEGAMIND.width);
    Vector3 image_pos = { shop->scroll, DISPLAY_Y - 0.20, 0.0 };
    Rectangle source = { 0.0,0.0, (float)MEGAMIND.width,(float)MEGAMIND.height };

    DrawBillboardPro(
        camera,
        MEGAMIND,
        source,
        image_pos,
        (Vector3){ 0.0, 1.0, 0.0 },
        (Vector2){ image_width, image_height },
        (Vector2){ image_width * 0.5, 0.0 },
        0.0,
        WHITE
    );

    Vector3 label_pos = { shop->scroll, DISPLAY_Y - 0.5, 0.5 };
    float width = 3.0;
    float height = 0.6;
    float depth = 0.10;
    Vector2 yaw_pitch = mouse_yaw_pitch(shop);
    rlPushMatrix();
    rlTranslatef(label_pos.x, label_pos.y, label_pos.z);
    rlRotatef(yaw_pitch.x, 0.0, 1.0, 0.0);
    rlRotatef(yaw_pitch.y, 1.0, 0.0, 0.0);
        DrawCube(
            (Vector3){ 0 },
            width, height, depth,
            RAYWHITE
        );
        DrawCubeWires(
            (Vector3){ 0 },
            width, height, depth,
            BLACK
        );
        DrawTextCentered3D(
            SHOP_FONT,
            "No Items??",
            (Vector3){ 0.0, 0.0, depth * 0.5 + 0.02 },
            0.32, 0.015,
            BLACK
        );
    rlPopMatrix();
}

Rectangle add_to_cart_button_bounds(Shop* shop) {
    float screen_w = shop->render_target.texture.width;
    float screen_h = shop->render_target.texture.height;

    return (Rectangle) {
        screen_w / 2.0f - 100.0f,
        screen_h - 100.0f,
        200.0f,
        60.0f
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

    if(shop->display.count > 0){
        if(button_event(shop, add_to_cart_button_bounds(shop))){
            int index = carousel_focused_item_index(
                shop->display,
                shop->scroll,
                DISPLAY_SPACING
            );

            Item item = shop->display.items[index];
            item.stock = item_available_stock(shop, item.id);

            add_to_cart(shop, item);
        }
    }
}

void draw_display(Shop* shop) {
    Camera3D camera = shop->camera;
    camera.position.x = shop->scroll;
    camera.target.x = shop->scroll;

    BeginMode3D(camera);
    if (shop->display.count == 0) {
        draw_no_items(shop, camera);
    } else {
        draw_carousel(
            shop,
            &shop->display,
            shop->scroll,
            DISPLAY_SPACING,
            DISPLAY_Y
        );
        int index = carousel_focused_item_index(shop->display, shop->scroll, DISPLAY_SPACING);
        Item live_item = shop->display.items[index];
        live_item.stock = item_available_stock(shop, live_item.id);
        draw_product_information_cube(
            shop, 
            live_item, 
            (Vector3){ shop->scroll, DISPLAY_Y, -1.0 }, 
            carousel_item_alpha(shop->display, shop->scroll, DISPLAY_SPACING)
        );
    }
    EndMode3D();

    if(shop->display.count > 0){
        int index = carousel_focused_item_index(
            shop->display, 
            shop->scroll, 
            DISPLAY_SPACING
        );

        Item item = shop->display.items[index];
        int stock = item_available_stock(shop, item.id);
        int in_cart = 0;
        for (int i = 0; i < shop->cart.count; i++) {
            if (shop->cart.items[i].item.id == item.id) {
                in_cart = shop->cart.items[i].quantity;
                break;
            }
        }

        if(stock > in_cart){
            draw_button(
                shop, 
                add_to_cart_button_bounds(shop), 
                "ADD TO CART", 
                SHOP_ORANGE
            );
        }
        else{
            draw_button(
                shop, 
                add_to_cart_button_bounds(shop), 
                "OUT OF STOCK", 
                SHOP_RED
            );
        }
    }
}
