/* Shows the current cart summary and places completed orders. */

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
    float screen_width = (float)shop->render_target.texture.width;
    float screen_height = (float)shop->render_target.texture.height;

    return (Rectangle){
        screen_width - 230.0f,
        screen_height - 90.0f,
        180.0f,
        45.0f
    };
}

static float checkout_subtotal(Shop* shop) {
    float subtotal = 0.0f;

    for (int item_index = 0; item_index < shop->cart.count; item_index++) {
        Cart_Item cart_item = shop->cart.items[item_index];

        float price = item_final_price(cart_item.item);
        subtotal += price * cart_item.quantity;
    }

    return subtotal;
}

static int checkout_summary_y(Shop* shop) {
    return 175 + (shop->cart.count * 62) + 25;
}

static Rectangle discount_textbox_bounds(Shop* shop) {
    int summary_y = checkout_summary_y(shop);
    return (Rectangle){50.0f, (float)(summary_y + 65), 280.0f, 40.0f};
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
    DrawText(displayed_text, (int)textbox.x + 12, (int)textbox.y + 10, 20, text_color);

    draw_button(shop, discount_apply_button_bounds(shop), "APPLY", SHOP_BLUE);
}

void update_checkout_button(Shop* shop) {
    if (button_event(shop, checkout_button_bounds(shop))) {
        shop->checkout_complete = false;
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

    update_discount_textbox(shop);

    if (button_event(shop, discount_apply_button_bounds(shop))) {
        discount_apply(shop, checkout_subtotal(shop));
    }

    if (button_event(shop, pay_button_bounds(shop))) {
        if (!shop->account.logged_in) {
            snprintf(shop->checkout_message, sizeof(shop->checkout_message),
                     "Log in before placing an order.");
            return;
        }

        float subtotal = checkout_subtotal(shop);
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

    int item_y = 175;
    const int left_x = 50;
    const int total_x = shop->render_target.texture.width - 210;

    for (int item_index = 0; item_index < shop->cart.count; item_index++) {
        Cart_Item cart_item = shop->cart.items[item_index];
        float final_price = item_final_price(cart_item.item);
        float line_total = final_price * cart_item.quantity;

        char item_name[256];
        char item_price_text[64];
        char line_total_text[64];
        format_money(item_price_text, sizeof(item_price_text), final_price);
        format_money(line_total_text, sizeof(line_total_text), line_total);

        // Keep long item names from overlapping the total.
        snprintf(item_name, sizeof(item_name), "%s x%d", cart_item.item.name,
                 cart_item.quantity);
        while (MeasureText(item_name, 22) > total_x - left_x - 24 &&
               strlen(item_name) > 4) {
            size_t len = strlen(item_name);
            item_name[len - 1] = '\0';
            item_name[len - 2] = '.';
            item_name[len - 3] = '.';
            item_name[len - 4] = '.';
        }
        DrawText(item_name, left_x, item_y, 22, BLACK);

        // Show the item total on the right.
        DrawText(line_total_text, total_x, item_y, 22, BLACK);

        if (item_is_on_sale(cart_item.item)) {
            char original_price_text[64];
            format_money(original_price_text, sizeof(original_price_text),
                         cart_item.item.price);

            int details_y = item_y + 28;
            DrawText("Was", left_x, details_y, 19, DARKGRAY);
            int original_x = left_x + MeasureText("Was", 19) + 10;
            DrawText(original_price_text, original_x, details_y, 19, DARKGRAY);
            int original_width = MeasureText(original_price_text, 19);
            DrawLine(original_x, details_y + 10,
                     original_x + original_width, details_y + 10, DARKGRAY);

            int sale_x = original_x + original_width + 28;
            char sale_label[96];
            snprintf(sale_label, sizeof(sale_label), "SALE %s", item_price_text);
            DrawText(sale_label, sale_x, details_y, 20, SHOP_GREEN);
        } else {
            char price_label[96];
            snprintf(price_label, sizeof(price_label), "Price %s each", item_price_text);
            DrawText(price_label, left_x, item_y + 28, 19, DARKGRAY);
        }

        item_y += 62;
    }

    float subtotal = checkout_subtotal(shop);
    float discount_amount = shop->applied_discount_amount;
    float taxable_amount = subtotal - discount_amount;
    float tax_amount = taxable_amount * 0.0825f;
    float total_amount = taxable_amount + tax_amount;

    int summary_y = checkout_summary_y(shop);

    char subtotal_price[64];
    char subtotal_text[96];
    format_money(subtotal_price, sizeof(subtotal_price), subtotal);
    snprintf(subtotal_text, sizeof(subtotal_text), "Subtotal: %s", subtotal_price);
    DrawText(subtotal_text, 50, summary_y, 24, BLACK);

    draw_discount_textbox(shop);

    char discount_price[64];
    char discount_text[96];
    format_money(discount_price, sizeof(discount_price), discount_amount);
    snprintf(discount_text, sizeof(discount_text), "Discount: -%s", discount_price);
    DrawText(discount_text, 50, summary_y + 118, 24,
             discount_amount > 0.0f ? SHOP_GREEN : DARKGRAY);

    if (shop->discount_message[0] != '\0') {
        DrawText(shop->discount_message, 50, summary_y + 150, 20,
                 shop->applied_discount_id > 0 ? SHOP_GREEN : MAROON);
    }

    char tax_price[64];
    char tax_text[96];
    format_money(tax_price, sizeof(tax_price), tax_amount);
    snprintf(tax_text, sizeof(tax_text), "Tax (8.25%%): %s", tax_price);
    DrawText(tax_text, 50, summary_y + 182, 24, BLACK);

    char total_price[64];
    char total_text[96];
    format_money(total_price, sizeof(total_price), total_amount);
    snprintf(total_text, sizeof(total_text), "Total: %s", total_price);
    DrawText(total_text, 50, summary_y + 222, 30, BLACK);

    draw_button(shop, pay_button_bounds(shop), "PAY", SHOP_GREEN);

    if (shop->checkout_message[0] != '\0') {
        DrawText(shop->checkout_message, 50, summary_y + 265, 22, MAROON);
    }
}
