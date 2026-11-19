/* Shows the current cart summary and places completed orders. */

enum {
    CHECKOUT_ITEMS_Y = 175,
    CHECKOUT_ROW_HEIGHT = 62,
};

Rectangle checkout_button_bounds(Shop* shop) {
    float screen_width = (float)shop->render_target.texture.width;
    float screen_height = (float)shop->render_target.texture.height;

    return (Rectangle){
        screen_width - 230.0f,
        screen_height - 90.0f,
        180.0f,
        45.0f
    };
}

static Rectangle checkout_home_button_bounds(Shop* shop) {
    float screen_width = (float)shop->render_target.texture.width;

    return (Rectangle){
        screen_width - 180.0f,
        45.0f,
        130.0f,
        45.0f
    };
}

static Rectangle pay_button_bounds(Shop* shop) {
    Rectangle bounds = back_button_bounds(shop);
    bounds.x = shop->render_target.texture.width - bounds.x - bounds.width;
    return bounds;
}

static int checkout_summary_y(Shop* shop) {
    return shop->render_target.texture.height - 228;
}

static int checkout_visible_item_count(Shop* shop) {
    return (checkout_summary_y(shop) - CHECKOUT_ITEMS_Y - 24) / CHECKOUT_ROW_HEIGHT;
}

static Rectangle checkout_scrollbar_track(Shop* shop) {
    return (Rectangle){shop->render_target.texture.width - 32, CHECKOUT_ITEMS_Y, 12,
                       checkout_summary_y(shop) - CHECKOUT_ITEMS_Y - 32};
}

static Rectangle checkout_scrollbar_thumb(Shop* shop, Rectangle track, int visible_count) {
    float height = fmaxf(24.0f, track.height * visible_count / shop->cart.count);
    float progress = (float)shop->checkout_first_visible_item / (shop->cart.count - visible_count);
    return (Rectangle){track.x, track.y + (track.height - height) * progress, track.width, height};
}

static bool update_checkout_scrollbar(Shop* shop, int visible_count) {
    int last_first_item = shop->cart.count - visible_count;
    if (last_first_item <= 0) {
        return false;
    }

    Rectangle track = checkout_scrollbar_track(shop);
    Rectangle thumb = checkout_scrollbar_thumb(shop, track, visible_count);
    Vector2 mouse = mouse_pos_in_shop(shop);
    if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT) && CheckCollisionPointRec(mouse, track)) {
        shop->checkout_scrollbar_grab = CheckCollisionPointRec(mouse, thumb)
            ? mouse.y - thumb.y : thumb.height / 2;
    }
    if (shop->checkout_scrollbar_grab < 0) {
        return false;
    }

    float progress = (mouse.y - track.y - shop->checkout_scrollbar_grab) / (track.height - thumb.height);
    shop->checkout_first_visible_item = (int)roundf(Clamp(progress, 0, 1) * last_first_item);
    if (!IsMouseButtonDown(MOUSE_BUTTON_LEFT)) {
        shop->checkout_scrollbar_grab = -1;
    }
    return true;
}

static Rectangle discount_textbox_bounds(Shop* shop) {
    int summary_y = checkout_summary_y(shop);
    return (Rectangle){50.0f, (float)(summary_y + 28), 280.0f, 40.0f};
}

static Rectangle discount_apply_button_bounds(Shop* shop) {
    Rectangle textbox = discount_textbox_bounds(shop);
    return (Rectangle){textbox.x + textbox.width + 15.0f, textbox.y, 120.0f, textbox.height};
}

static void update_discount_textbox(Shop* shop) {
    Rectangle textbox = discount_textbox_bounds(shop);
    Vector2 mouse = mouse_pos_in_shop(shop);

    if (IsMouseButtonReleased(MOUSE_BUTTON_LEFT)) {
        shop->discount_box_active = CheckCollisionPointRec(mouse, textbox);
    }

    if (!shop->discount_box_active) {
        return;
    }

    int character = GetCharPressed();
    while (character > 0) {
        if (character >= 32 && character <= 126) {
            size_t current_length = strlen(shop->discount_code);
            if (current_length + 1 < sizeof(shop->discount_code)) {
                shop->discount_code[current_length] = (char)character;
                shop->discount_code[current_length + 1] = '\0';
                shop->applied_discount_id = 0;
                shop->applied_discount_amount = 0.0f;
                shop->discount_message[0] = '\0';
            }
        }
        character = GetCharPressed();
    }

    if (IsKeyPressed(KEY_BACKSPACE) || IsKeyPressedRepeat(KEY_BACKSPACE)) {
        size_t current_length = strlen(shop->discount_code);
        if (current_length > 0) {
            shop->discount_code[current_length - 1] = '\0';
            shop->applied_discount_id = 0;
            shop->applied_discount_amount = 0.0f;
            shop->discount_message[0] = '\0';
        }
    }
}

static void draw_discount_textbox(Shop* shop) {
    Rectangle textbox = discount_textbox_bounds(shop);
    Color border_color = shop->discount_box_active ? SHOP_GREEN : GRAY;

    DrawText("Discount Code", 50, (int)textbox.y - 28, 22, BLACK);
    DrawRectangleRec(textbox, RAYWHITE);
    DrawRectangleLinesEx(textbox, 2.0f, border_color);

    const char* displayed_text = shop->discount_code[0] != '\0' ? shop->discount_code : "Enter code";
    Color text_color = shop->discount_code[0] != '\0' ? BLACK : GRAY;
    int text_x = (int)textbox.x + 12;
    int text_width = (int)textbox.width - 24;
    int overflow = MAX(0, MeasureText(displayed_text, 20) - text_width);
    BeginScissorMode(text_x, (int)textbox.y, text_width, (int)textbox.height);
    DrawText(displayed_text, text_x - overflow, (int)textbox.y + 10, 20, text_color);
    EndScissorMode();

    draw_button(shop, discount_apply_button_bounds(shop), "APPLY", SHOP_BLUE);
}

void update_checkout_button(Shop* shop) {
    if (button_event(shop, checkout_button_bounds(shop))) {
        cart_remove_zero_quantity_items(shop);
        shop->checkout_complete = false;
        shop->checkout_first_visible_item = 0;
        shop->checkout_scrollbar_grab = -1;
        shop->checkout_message[0] = '\0';
        discount_reset(shop);
        screen_swap(shop, CHECKOUT_SCREEN);
    }
}

void draw_checkout_button(Shop* shop) {
    draw_button(
        shop,
        checkout_button_bounds(shop),
        "CHECKOUT",
        SHOP_GREEN
    );
}

void update_checkout(Shop* shop) {
    int visible_count = checkout_visible_item_count(shop);
    if (update_checkout_scrollbar(shop, visible_count)) {
        return;
    }

    if (button_event(shop, checkout_home_button_bounds(shop))) {
        screen_swap(shop, HOME_SCREEN);
        return;
    }

    if (button_event(shop, back_button_bounds(shop))) {
        screen_swap(shop, CART_SCREEN);
        return;
    }

    if (shop->checkout_complete || shop->cart.count == 0) {
        return;
    }

    float wheel = GetMouseWheelMove();
    int last_first_item = MAX(0, shop->cart.count - visible_count);
    if (wheel > 0) shop->checkout_first_visible_item--;
    if (wheel < 0) shop->checkout_first_visible_item++;
    shop->checkout_first_visible_item = MAX(0, MIN(shop->checkout_first_visible_item, last_first_item));

    update_discount_textbox(shop);

    if (button_event(shop, discount_apply_button_bounds(shop))) {
        discount_apply(shop, cart_subtotal(shop));
    }

    if (button_event(shop, pay_button_bounds(shop))) {
        if (!shop->account.logged_in) {
            snprintf(shop->checkout_message, sizeof(shop->checkout_message),
                     "Log in before placing an order.");
            return;
        }

        if (!sales_refresh(shop)) {
            snprintf(shop->checkout_message, sizeof(shop->checkout_message), "Could not refresh prices. Try again.");
            return;
        }
        if (shop->discount_code[0]) discount_apply(shop, cart_subtotal(shop));
        float subtotal = cart_subtotal(shop);
        float discount_amount = shop->applied_discount_amount;
        float taxable_amount = subtotal - discount_amount;
        float tax_amount = taxable_amount * 0.0825f;
        float total_amount = taxable_amount + tax_amount;

        if (order_save_purchase(shop, shop->applied_discount_id, subtotal,
                                discount_amount, tax_amount, total_amount)) {
            cart_clear(shop);
            cart_save(shop);
            shop->checkout_complete = true;
            snprintf(shop->checkout_message, sizeof(shop->checkout_message),
                     "Thank you for your purchase!");
        } else {
            snprintf(shop->checkout_message, sizeof(shop->checkout_message),
                     "Could not complete purchase. Check item stock and try again.");
        }
    }
}

static void draw_checkout_right(const char* text, int right, int y, int size, Color color) {
    DrawText(text, right - MeasureText(text, size), y, size, color);
}

void draw_checkout(Shop* shop) {
    DrawText("CHECKOUT", 50, 50, 40, BLACK);

    draw_button(
        shop,
        back_button_bounds(shop),
        "< CART",
        SHOP_GREEN
    );

    draw_button(
        shop,
        checkout_home_button_bounds(shop),
        "HOME",
        SHOP_BLUE
    );

    if (shop->checkout_complete) {
        DrawText("Thank you for your purchase!", 50, 150, 34, SHOP_GREEN);
        DrawText("Your order is saved in Past Purchases.", 50, 205, 24, DARKGRAY);
        return;
    }

    DrawText("ORDER SUMMARY", 50, 120, 28, BLACK);

    if (shop->cart.count == 0) {
        DrawText("Your cart is empty.", 50, 175, 24, DARKGRAY);
        return;
    }

    int item_y = CHECKOUT_ITEMS_Y;
    const int left_x = 50;
    const int right_x = shop->render_target.texture.width - 50;
    int visible_count = checkout_visible_item_count(shop);
    int first_item = shop->checkout_first_visible_item;
    int end_item = MIN(first_item + visible_count, shop->cart.count);

    for (int item_index = first_item; item_index < end_item; item_index++) {
        Cart_Item cart_item = shop->cart.items[item_index];
        float final_price = item_final_price(cart_item.item);
        float line_total = final_price * cart_item.quantity;

        char item_name[256];
        char item_price_text[64];
        char line_total_text[64];
        format_money(item_price_text, sizeof(item_price_text), final_price);
        format_money(line_total_text, sizeof(line_total_text), line_total);

        // Keep long item names from overlapping the total.
        snprintf(item_name, sizeof(item_name), "%s x%d", cart_item.item.display,
                 cart_item.quantity);
        int item_name_width = right_x - MeasureText(line_total_text, 22) - left_x - 24;
        while (MeasureText(item_name, 22) > item_name_width && strlen(item_name) > 4) {
            size_t len = strlen(item_name);
            item_name[len - 1] = '\0';
            item_name[len - 2] = '.';
            item_name[len - 3] = '.';
            item_name[len - 4] = '.';
        }
        DrawText(item_name, left_x, item_y, 22, BLACK);

        // Show the item total on the right.
        draw_checkout_right(line_total_text, right_x, item_y, 22, BLACK);

        if (item_is_on_sale(cart_item.item)) {
            char original_price_text[64];
            format_money(original_price_text, sizeof(original_price_text),
                         cart_item.item.price);

            int details_y = item_y + 28;
            int details_size = 20;
            DrawText("Was", left_x, details_y, details_size, DARKGRAY);
            int original_x = left_x + MeasureText("Was", details_size) + 10;
            DrawText(original_price_text, original_x, details_y, details_size, DARKGRAY);
            int original_width = MeasureText(original_price_text, details_size);
            int strike_y = details_y + details_size / 2;
            DrawLine(original_x, strike_y, original_x + original_width, strike_y, DARKGRAY);

            int sale_x = original_x + original_width + 28;
            char sale_label[96];
            snprintf(sale_label, sizeof(sale_label), "SALE %s", item_price_text);
            DrawText(sale_label, sale_x, details_y, details_size, SHOP_GREEN);
        } else {
            char price_label[96];
            snprintf(price_label, sizeof(price_label), "Price %s each", item_price_text);
            DrawText(price_label, left_x, item_y + 28, 19, DARKGRAY);
        }

        item_y += CHECKOUT_ROW_HEIGHT;
    }

    float subtotal = cart_subtotal(shop);
    float discount_amount = shop->applied_discount_amount;
    float taxable_amount = subtotal - discount_amount;
    float tax_amount = taxable_amount * 0.0825f;
    float total_amount = taxable_amount + tax_amount;

    int summary_y = checkout_summary_y(shop);
    if (shop->cart.count > visible_count) {
        Rectangle track = checkout_scrollbar_track(shop);
        DrawRectangleRec(track, SHOP_SHADOW);
        DrawRectangleRec(checkout_scrollbar_thumb(shop, track, visible_count), SHOP_INK);
    }
    DrawLine(left_x, summary_y - 20, right_x, summary_y - 20, SHOP_SHADOW);

    char subtotal_price[64];
    char subtotal_text[96];
    format_money(subtotal_price, sizeof(subtotal_price), subtotal);
    snprintf(subtotal_text, sizeof(subtotal_text), "Subtotal: %s", subtotal_price);
    draw_checkout_right(subtotal_text, right_x, summary_y, 24, BLACK);

    draw_discount_textbox(shop);

    char discount_price[64];
    char discount_text[96];
    format_money(discount_price, sizeof(discount_price), discount_amount);
    snprintf(discount_text, sizeof(discount_text), "Discount: -%s", discount_price);
    draw_checkout_right(discount_text, right_x, summary_y + 30, 22,
                        discount_amount > 0.0f ? SHOP_GREEN : DARKGRAY);

    if (shop->discount_message[0] != '\0') {
        DrawText(shop->discount_message, left_x, summary_y + 78, 18,
                 shop->applied_discount_id > 0 ? SHOP_GREEN : MAROON);
    }

    char tax_price[64];
    char tax_text[96];
    format_money(tax_price, sizeof(tax_price), tax_amount);
    snprintf(tax_text, sizeof(tax_text), "Tax (8.25%%): %s", tax_price);
    draw_checkout_right(tax_text, right_x, summary_y + 58, 22, BLACK);

    char total_price[64];
    char total_text[96];
    format_money(total_price, sizeof(total_price), total_amount);
    snprintf(total_text, sizeof(total_text), "Total: %s", total_price);
    draw_checkout_right(total_text, right_x, summary_y + 86, 28, BLACK);

    draw_button(shop, pay_button_bounds(shop), "PAY", SHOP_GREEN);

    if (shop->checkout_message[0] != '\0') {
        Rectangle back_button = back_button_bounds(shop);
        DrawText(shop->checkout_message, (int)(back_button.x + back_button.width) + 20,
                 (int)(back_button.y + (back_button.height - 18) / 2), 18, MAROON);
    }
}
