
#define DISPLAY_Y -1.2
#define DISPLAY_SPACING 2.0

void init_shop_items(Shop* shop) {
    shop->camera.position = (Vector3){ 0.0f, DISPLAY_Y, 7.0f };
    shop->camera.target = (Vector3){ 0.0f, DISPLAY_Y, 0.0f };
    shop->camera.up = (Vector3){ 0.0f, 1.0f, 0.0f };
    shop->camera.fovy = 45.0f;
    shop->camera.projection = CAMERA_PERSPECTIVE;
}

void update_display(Shop* shop) {
    update_carousel(
        &shop->scroll,
        &shop->scroll_target,
        shop->display_items.count,
        DISPLAY_SPACING,
        true
    );
}

void draw_display(Shop* shop) {
    draw_carousel(
        shop,
        &shop->display_items,
        shop->scroll,
        DISPLAY_SPACING,
        DISPLAY_Y
    );
}
