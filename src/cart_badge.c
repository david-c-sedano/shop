/* Shows the total number of selected cart items on the cart button. */

int cart_selected_item_count(Shop* shop) {
    int selected_item_count = 0;

    for (int item_index = 0; item_index < shop->cart.count; item_index++) {
        selected_item_count += shop->cart.items[item_index].quantity;
    }

    return selected_item_count;
}

void draw_cart_button_with_count(Shop* shop) {
    Rectangle button_bounds = cart_button_bounds(shop);
    draw_button(shop, button_bounds, "CART", SHOP_ORANGE);

    int selected_item_count = cart_selected_item_count(shop);
    if (selected_item_count <= 0) {
        return;
    }

    char count_text[16];
    if (selected_item_count > 99) {
        snprintf(count_text, sizeof(count_text), "99+");
    } else {
        snprintf(count_text, sizeof(count_text), "%d", selected_item_count);
    }

    float badge_x = button_bounds.x + button_bounds.width - 4.0f;
    float badge_y = button_bounds.y + 4.0f;
    float badge_radius = 15.0f;

    DrawCircleV((Vector2){badge_x, badge_y}, badge_radius, SHOP_BLUE);

    int font_size = 18;
    int text_width = MeasureText(count_text, font_size);
    DrawText(
        count_text,
        (int)(badge_x - (float)text_width * 0.5f),
        (int)(badge_y - (float)font_size * 0.5f),
        font_size,
        WHITE
    );
}
