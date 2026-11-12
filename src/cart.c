// Get the latest stock from the database.
int item_available_stock(Shop* shop, int item_id) {
    sqlite3_stmt* stmt = NULL;
    int stock = 0;
    if (sqlite3_prepare_v2(shop->admin.db,
            "SELECT stock FROM items WHERE id = ?1;", -1, &stmt, NULL) != SQLITE_OK)
        return 0;
    sqlite3_bind_int(stmt, 1, item_id);
    if (sqlite3_step(stmt) == SQLITE_ROW)
        stock = sqlite3_column_int(stmt, 0);
    sqlite3_finalize(stmt);
    return stock > 0 ? stock : 0;
}

Rectangle cart_minus_button_bounds(float x, int y) {
    return (Rectangle){x, (float)y, 40.0f, 30.0f};
}

Rectangle cart_plus_button_bounds(float x, int y) {
    return (Rectangle){x + 50.0f, (float)y, 40.0f, 30.0f};
}

// Use the same item name for the label and buttons.
static void cart_item_label(Cart_Item cart_item, char* text, size_t text_size) {
    if (item_is_on_sale(cart_item.item)) {
        snprintf(text, text_size, "%s x%d  Was $%.2f  |  SALE $%.2f",
                 cart_item.item.name, cart_item.quantity,
                 cart_item.item.price, item_final_price(cart_item.item));
    } else {
        snprintf(text, text_size, "%s x%d  $%.2f",
                 cart_item.item.name, cart_item.quantity, cart_item.item.price);
    }
}

void draw_cart(Shop* shop) {
    DrawText("SHOPPING CART", 50, 50, 40, BLACK);
    draw_button(shop, back_button_bounds(shop), "< BACK", SHOP_GREEN);

    int y = 120;
    for (int i = 0; i < shop->cart.count; i++) {
        Cart_Item cart_item = shop->cart.items[i];
        char text[256];
        cart_item_label(cart_item, text, sizeof(text));
        DrawText(text, 40, y, 20, BLACK);

        float button_x = 40.0f + MeasureText(text, 20) + 15.0f;
        draw_button(shop, cart_minus_button_bounds(button_x, y), "-", SHOP_RED);
        draw_button(shop, cart_plus_button_bounds(button_x, y), "+", SHOP_GREEN);
        y += 40;
    }
}

void update_cart(Shop* shop) {
    if (button_event(shop, back_button_bounds(shop))) {
        screen_swap(shop, DISPLAY_SCREEN);
        return;
    }

    int y = 120;

    for (int i = 0; i < shop->cart.count; i++) {
        Cart_Item cart_item = shop->cart.items[i];

        char text[256];
        cart_item_label(cart_item, text, sizeof(text));

        // Calculate button position based on text width
        float button_x = 40.0f + MeasureText(text, 20) + 15.0f;

        // PLUS BUTTON
        if (button_event(shop, cart_plus_button_bounds(button_x, y))) {
            if (shop->cart.items[i].quantity < item_available_stock(shop, shop->cart.items[i].item.id)) {
                shop->cart.items[i].quantity++;
            }
        }

        // MINUS BUTTON
        if (button_event(shop, cart_minus_button_bounds(button_x, y))) {
            if (shop->cart.items[i].quantity > 1) {
                shop->cart.items[i].quantity--;
            } else {
                // Remove item when quantity reaches zero
                for (int j = i; j < shop->cart.count - 1; j++) {
                    shop->cart.items[j] = shop->cart.items[j + 1];
                }

                shop->cart.count--;
                y += 40;
                continue;
            }
        }

        y += 40;
    }
}

void add_to_cart(Shop* shop, Item item) {
    // Check if item already exists in cart
    for (int i = 0; i < shop->cart.count; i++) {
        if (shop->cart.items[i].item.id == item.id) {

            // Don't go above available stock
            if (shop->cart.items[i].quantity < item_available_stock(shop, item.id)) {
                shop->cart.items[i].quantity++;
            }

            return;
        }
    }

    // Don't add out-of-stock items
    if (item_available_stock(shop, item.id) <= 0) {
        return;
    }

    // Grow array if necessary
    if (shop->cart.count >= shop->cart.capacity) {
        shop->cart.capacity =
            shop->cart.capacity == 0
                ? 4
                : shop->cart.capacity * 2;

        shop->cart.items = realloc(
            shop->cart.items,
            shop->cart.capacity * sizeof(Cart_Item)
        );
    }

    // Add new item
    shop->cart.items[shop->cart.count] = (Cart_Item) {
        .item = item,
        .quantity = 1
    };

    shop->cart.count++;
}