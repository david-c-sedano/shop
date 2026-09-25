
// this is in 3D units
#define HOME_FEATURED_SPACING 4.0
#define HOME_FEATURED_Y       -0.3

#define HOME_BUTTON_SPACING 300.0
#define HOME_BUTTON_X       500.0
#define HOME_BUTTON_Y       610.0
#define HOME_BUTTON_SIZE    250.0

void add_home_button(Shop* shop, Home_Button button) {
    shop->home_button_count+=1;
    shop->home_buttons = realloc(
        shop->home_buttons, 
        shop->home_button_count * sizeof(*shop->home_buttons)
    );
    shop->home_buttons[shop->home_button_count-1] = button;
}

Rectangle home_button_rect(Shop* shop, int index) {
    float item_x = index * HOME_BUTTON_SPACING;
    float center_dist = fabsf(item_x - shop->bottom_row_scroll);
    float focus = 1.0 - Clamp(center_dist / HOME_BUTTON_SPACING, 0.0, 1.0);
    float scale = Lerp(0.70, 1.0, focus);
    float x = HOME_BUTTON_X + item_x - shop->bottom_row_scroll;
    float w = HOME_BUTTON_SIZE * scale;
    float h = HOME_BUTTON_SIZE * scale;
    return (Rectangle) {
        x - w * 0.5, HOME_BUTTON_Y - h * 0.5,
        w, h
    };
}

void update_home(Shop* shop) {
    Vector2 mouse = mouse_pos_in_shop(shop);
    bool top_active = mouse.y < 500;
    update_carousel(
        &shop->top_row_scroll, &shop->top_row_scroll_target,
        shop->featured.count, HOME_FEATURED_SPACING,
        top_active
    );
    update_carousel(
        &shop->bottom_row_scroll, &shop->bottom_row_scroll_target,
        shop->home_button_count, HOME_BUTTON_SPACING,
        !top_active
    );

    if (!IsMouseButtonReleased(MOUSE_BUTTON_LEFT)) {
        return;
    }
    for (int i=0; i<shop->home_button_count; i++) {
        if (!CheckCollisionPointRec(mouse, home_button_rect(shop, i))) {
            continue;
        }
        // scroll if not clicking on the centered button
        float button_scroll = i * HOME_BUTTON_SPACING;
        if (fabsf(shop->bottom_row_scroll - button_scroll) > 10.0f) {
            shop->bottom_row_scroll_target = button_scroll;
            return;
        }
        // Retained mode GUI be like:
        Home_Button button = shop->home_buttons[i];
        if (button.callback) {
            button.callback(shop);
        }
        screen_swap(shop, button.transition);
    }
}

void draw_home_buttons(Shop* shop) {
    Vector2 mouse = mouse_pos_in_shop(shop);
    for (int i=0; i<shop->home_button_count; i++) {
        Home_Button button = shop->home_buttons[i];
        Rectangle source = {
            0,0,
            button.texture.width, button.texture.height
        };
        Rectangle dest = home_button_rect(shop, i);
        bool hovered = CheckCollisionPointRec(mouse, dest) && !shop->paused;
        bool pressed = hovered && IsMouseButtonDown(MOUSE_BUTTON_LEFT);

        Color shade = (Color){200, 200, 200, 255};

        if (pressed && !shop->paused) {
            float scale = 0.90f;
            float old_w = dest.width;
            float old_h = dest.height;
            dest.width *= scale;
            dest.height *= scale;
            dest.x += (old_w - dest.width) * 0.5f;
            dest.y += (old_h - dest.height) * 0.5f;
        }

        Color tint = hovered ? WHITE : shade;
        DrawTexturePro(button.texture, source, dest, (Vector2){0,0}, 0.0, tint);
    }
}

void draw_featured_tag(Shop* shop, Item item, Vector3 pos, float alpha) {
    float width = 3.0;
    float height = 0.8;
    float depth = 0.12;
    pos.y += 2.3;

    Vector2 yaw_pitch = mouse_yaw_pitch(shop);
    float yaw = yaw_pitch.x;
    float pitch = yaw_pitch.y;

    rlPushMatrix();
    rlTranslatef(pos.x, pos.y, pos.z);
    rlRotatef(yaw,   0.0, 1.0, 0.0);
    rlRotatef(pitch, 1.0, 0.0, 0.0);
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

        float text_z = depth * 0.5 + 0.8;
        DrawTextCentered3D(
            SHOP_FONT, 
            "FEATURED", 
            (Vector3){ 0.0, 0.02, text_z },
            0.16, 0.008,
            Fade(DARKGRAY, alpha)
        );
        DrawTextCentered3D(
            SHOP_FONT,
            item.display,
            (Vector3){ 0.0, -0.33, text_z },
            0.30, 0.012, 
            Fade(BLACK, alpha)
        );
    rlPopMatrix();
}

void draw_home(Shop* shop) {
    Camera3D camera = shop->camera;
    camera.position.x = shop->top_row_scroll;
    camera.target.x = shop->top_row_scroll;
    BeginMode3D(camera);
    // top row
    draw_carousel(
        shop, 
        &shop->featured, 
        shop->top_row_scroll, 
        HOME_FEATURED_SPACING,
        HOME_FEATURED_Y
    );

    int index = carousel_focused_item_index(shop->featured, shop->top_row_scroll, HOME_FEATURED_SPACING);
    if (shop->featured.count > 0) {
        draw_featured_tag(
            shop, 
            shop->featured.items[index],
            (Vector3){ shop->top_row_scroll, HOME_FEATURED_Y, -1.0 },
            carousel_item_alpha(shop->featured, shop->top_row_scroll, HOME_FEATURED_SPACING)
        );
    }
    EndMode3D();

    // bottom row
    draw_home_buttons(shop);
}

void chairs_display_callback(Shop* shop) {
    shop->display = query_items(shop,
        "SELECT " ITEM_COLUMNS " FROM items "
        "WHERE category = 'chairs';"
    );
}
void furniture_display_callback(Shop* shop) {
    shop->display = query_items(shop,
        "SELECT " ITEM_COLUMNS " FROM items "
        "WHERE category = 'furniture';"
    );
}
void large_display_callback(Shop* shop) {
    shop->display = query_items(shop,
        "SELECT " ITEM_COLUMNS " FROM items "
        "WHERE category = 'large';"
    );
}

void init_home_menu_buttons(Shop* shop) {
    add_home_button(shop, (Home_Button) {
        .texture = CHAIRS_HOME_ICON,
        .transition = DISPLAY_SCREEN,
        .callback = chairs_display_callback
    });
    add_home_button(shop, (Home_Button) {
        .texture = OTHER_FURNISHINGS_HOME_ICON,
        .transition = DISPLAY_SCREEN,
        .callback = furniture_display_callback
    });
    add_home_button(shop, (Home_Button) {
        .texture = LARGER_ITEMS_HOME_ICON,
        .transition = DISPLAY_SCREEN,
        .callback = large_display_callback
    });
    add_home_button(shop, (Home_Button) {
        .texture = ACCOUNT_SETTINGS_HOME_ICON,
        .transition = ACCOUNT_SCREEN,
        .callback = NULL
    });
}
