/* Saves a logged-in user's cart whenever the cart contents change. */

typedef struct {
    int item_id;
    int quantity;
} Saved_Cart_Item;

static Saved_Cart_Item* last_saved_items = NULL;
static int last_saved_count = 0;
static int last_saved_user_id = 0;

static bool cart_make_room(Cart* cart) {
    if (cart->count < cart->capacity) {
        return true;
    }

    int new_capacity = cart->capacity == 0 ? 4 : cart->capacity * 2;
    Cart_Item* new_items = realloc(cart->items, (size_t)new_capacity * sizeof(Cart_Item));

    if (new_items == NULL) {
        return false;
    }

    cart->items = new_items;
    cart->capacity = new_capacity;
    return true;
}

static void cart_copy_text(char* destination, size_t destination_size,
                           sqlite3_stmt* statement, int column_index) {
    const unsigned char* text = sqlite3_column_text(statement, column_index);
    snprintf(destination, destination_size, "%s", text != NULL ? (const char*)text : "");
}

static bool cart_matches_last_save(Shop* shop) {
    if (last_saved_user_id != shop->account.user_id ||
        last_saved_count != shop->cart.count) {
        return false;
    }

    for (int index = 0; index < shop->cart.count; index++) {
        Cart_Item* cart_item = &shop->cart.items[index];
        Saved_Cart_Item* saved_item = &last_saved_items[index];

        if (cart_item->item.id != saved_item->item_id ||
            cart_item->quantity != saved_item->quantity) {
            return false;
        }
    }

    return true;
}

static bool remember_current_cart(Shop* shop) {
    if (shop->cart.count == 0) {
        free(last_saved_items);
        last_saved_items = NULL;
        last_saved_count = 0;
        last_saved_user_id = shop->account.user_id;
        return true;
    }

    Saved_Cart_Item* saved_items = realloc(
        last_saved_items,
        (size_t)shop->cart.count * sizeof(Saved_Cart_Item)
    );

    if (saved_items == NULL) {
        return false;
    }

    last_saved_items = saved_items;

    for (int index = 0; index < shop->cart.count; index++) {
        last_saved_items[index].item_id = shop->cart.items[index].item.id;
        last_saved_items[index].quantity = shop->cart.items[index].quantity;
    }

    last_saved_count = shop->cart.count;
    last_saved_user_id = shop->account.user_id;
    return true;
}

bool cart_save_init(Shop* shop) {
    const char* sql =
        "CREATE TABLE IF NOT EXISTS account_saved_cart_items ("
            "user_id INTEGER NOT NULL,"
            "item_id INTEGER NOT NULL,"
            "quantity INTEGER NOT NULL CHECK(quantity > 0),"
            "PRIMARY KEY (user_id, item_id),"
            "FOREIGN KEY (user_id) REFERENCES users(id) ON DELETE CASCADE"
        ");";

    return sqlite3_exec(shop->admin.db, sql, NULL, NULL, NULL) == SQLITE_OK;
}

void cart_clear(Shop* shop) {
    free(shop->cart.items);
    shop->cart.items = NULL;
    shop->cart.count = 0;
    shop->cart.capacity = 0;
}

bool cart_save(Shop* shop) {
    if (!shop->account.logged_in || shop->account.user_id <= 0) {
        return false;
    }

    const char* delete_sql =
        "DELETE FROM account_saved_cart_items WHERE user_id = ?1;";

    sqlite3_stmt* delete_statement = NULL;
    if (sqlite3_prepare_v2(shop->admin.db, delete_sql, -1, &delete_statement, NULL) != SQLITE_OK) {
        return false;
    }

    sqlite3_bind_int(delete_statement, 1, shop->account.user_id);
    bool delete_success = sqlite3_step(delete_statement) == SQLITE_DONE;
    sqlite3_finalize(delete_statement);

    if (!delete_success) {
        return false;
    }

    const char* insert_sql =
        "INSERT INTO account_saved_cart_items(user_id, item_id, quantity) VALUES(?1, ?2, ?3);";

    sqlite3_stmt* insert_statement = NULL;
    if (sqlite3_prepare_v2(shop->admin.db, insert_sql, -1, &insert_statement, NULL) != SQLITE_OK) {
        return false;
    }

    for (int index = 0; index < shop->cart.count; index++) {
        Cart_Item* cart_item = &shop->cart.items[index];

        sqlite3_reset(insert_statement);
        sqlite3_clear_bindings(insert_statement);
        sqlite3_bind_int(insert_statement, 1, shop->account.user_id);
        sqlite3_bind_int(insert_statement, 2, cart_item->item.id);
        sqlite3_bind_int(insert_statement, 3, cart_item->quantity);

        if (sqlite3_step(insert_statement) != SQLITE_DONE) {
            sqlite3_finalize(insert_statement);
            return false;
        }
    }

    sqlite3_finalize(insert_statement);
    return remember_current_cart(shop);
}

bool cart_load(Shop* shop) {
    if (!shop->account.logged_in || shop->account.user_id <= 0) {
        return false;
    }

    cart_clear(shop);

    const char* sql =
        "SELECT items.id, items.name, items.description, items.price, items.stock, "
        "items.category, items.display, saved.quantity, items.sale_price "
        "FROM account_saved_cart_items AS saved "
        "JOIN items ON items.id = saved.item_id "
        "WHERE saved.user_id = ?1;";

    sqlite3_stmt* statement = NULL;
    if (sqlite3_prepare_v2(shop->admin.db, sql, -1, &statement, NULL) != SQLITE_OK) {
        return false;
    }

    sqlite3_bind_int(statement, 1, shop->account.user_id);

    while (sqlite3_step(statement) == SQLITE_ROW) {
        if (!cart_make_room(&shop->cart)) {
            sqlite3_finalize(statement);
            cart_clear(shop);
            return false;
        }

        Cart_Item* cart_item = &shop->cart.items[shop->cart.count];
        memset(cart_item, 0, sizeof(*cart_item));

        cart_item->item.id = sqlite3_column_int(statement, 0);
        cart_copy_text(cart_item->item.name, sizeof(cart_item->item.name), statement, 1);
        cart_copy_text(cart_item->item.description, sizeof(cart_item->item.description), statement, 2);
        cart_item->item.price = (float)sqlite3_column_double(statement, 3);
        cart_item->item.stock = sqlite3_column_int(statement, 4);
        cart_copy_text(cart_item->item.category, sizeof(cart_item->item.category), statement, 5);
        cart_copy_text(cart_item->item.display, sizeof(cart_item->item.display), statement, 6);
        cart_item->quantity = sqlite3_column_int(statement, 7);
        cart_item->item.sale_price = (float)sqlite3_column_double(statement, 8);

        if (cart_item->quantity > cart_item->item.stock) {
            cart_item->quantity = cart_item->item.stock;
        }

        if (cart_item->quantity > 0) {
            shop->cart.count++;
        }
    }

    sqlite3_finalize(statement);
    return remember_current_cart(shop);
}

void cart_save_if_changed(Shop* shop) {
    if (!shop->account.logged_in || shop->account.user_id <= 0) {
        return;
    }

    if (!cart_matches_last_save(shop)) {
        cart_save(shop);
    }
}
