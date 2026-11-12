/* Saves completed checkout orders for the logged-in account. */

bool order_save_init(Shop* shop) {
    const char* order_schema =
        "CREATE TABLE IF NOT EXISTS orders ("
            "id INTEGER PRIMARY KEY AUTOINCREMENT,"
            "user_id INTEGER NOT NULL,"
            "discount_code_id INTEGER,"
            "subtotal NUMERIC NOT NULL,"
            "discount_amount NUMERIC NOT NULL DEFAULT 0,"
            "tax_amount NUMERIC NOT NULL,"
            "total_amount NUMERIC NOT NULL,"
            "created_at TEXT DEFAULT CURRENT_TIMESTAMP,"
            "FOREIGN KEY (user_id) REFERENCES users(id),"
            "FOREIGN KEY (discount_code_id) REFERENCES discount_codes(id)"
        ");"
        "CREATE TABLE IF NOT EXISTS order_items ("
            "id INTEGER PRIMARY KEY AUTOINCREMENT,"
            "order_id INTEGER NOT NULL,"
            "product_id INTEGER NOT NULL,"
            "product_name TEXT NOT NULL,"
            "quantity INTEGER NOT NULL,"
            "unit_price NUMERIC NOT NULL,"
            "FOREIGN KEY (order_id) REFERENCES orders(id) ON DELETE CASCADE"
        ");";

    return sqlite3_exec(shop->admin.db, order_schema, NULL, NULL, NULL) == SQLITE_OK;
}

bool order_save_purchase(Shop* shop, int discount_code_id, float subtotal, float discount_amount,
                         float tax_amount, float total_amount) {
    if (!shop->account.logged_in || shop->account.user_id <= 0 || shop->cart.count == 0) {
        return false;
    }

    sqlite3* database = shop->admin.db;
    if (sqlite3_exec(database, "BEGIN TRANSACTION;", NULL, NULL, NULL) != SQLITE_OK) {
        return false;
    }

    const char* order_sql =
        "INSERT INTO orders(user_id, discount_code_id, subtotal, discount_amount, tax_amount, total_amount) "
        "VALUES(?1, ?2, ?3, ?4, ?5, ?6);";

    sqlite3_stmt* order_statement = NULL;
    if (sqlite3_prepare_v2(database, order_sql, -1, &order_statement, NULL) != SQLITE_OK) {
        sqlite3_exec(database, "ROLLBACK;", NULL, NULL, NULL);
        return false;
    }

    sqlite3_bind_int(order_statement, 1, shop->account.user_id);
    if (discount_code_id > 0) {
        sqlite3_bind_int(order_statement, 2, discount_code_id);
    } else {
        sqlite3_bind_null(order_statement, 2);
    }
    sqlite3_bind_double(order_statement, 3, subtotal);
    sqlite3_bind_double(order_statement, 4, discount_amount);
    sqlite3_bind_double(order_statement, 5, tax_amount);
    sqlite3_bind_double(order_statement, 6, total_amount);

    bool order_saved = sqlite3_step(order_statement) == SQLITE_DONE;
    sqlite3_finalize(order_statement);

    if (!order_saved) {
        sqlite3_exec(database, "ROLLBACK;", NULL, NULL, NULL);
        return false;
    }

    sqlite3_int64 order_id = sqlite3_last_insert_rowid(database);
    const char* item_sql =
        "INSERT INTO order_items(order_id, product_id, product_name, quantity, unit_price) "
        "VALUES(?1, ?2, ?3, ?4, ?5);";

    sqlite3_stmt* item_statement = NULL;
    if (sqlite3_prepare_v2(database, item_sql, -1, &item_statement, NULL) != SQLITE_OK) {
        sqlite3_exec(database, "ROLLBACK;", NULL, NULL, NULL);
        return false;
    }

    bool items_saved = true;

    for (int item_index = 0; item_index < shop->cart.count; item_index++) {
        Cart_Item* cart_item = &shop->cart.items[item_index];

        sqlite3_bind_int64(item_statement, 1, order_id);
        sqlite3_bind_int(item_statement, 2, cart_item->item.id);
        sqlite3_bind_text(item_statement, 3, cart_item->item.name, -1, SQLITE_TRANSIENT);
        sqlite3_bind_int(item_statement, 4, cart_item->quantity);
        sqlite3_bind_double(item_statement, 5, item_final_price(cart_item->item));

        if (sqlite3_step(item_statement) != SQLITE_DONE) {
            items_saved = false;
            break;
        }

        sqlite3_reset(item_statement);
        sqlite3_clear_bindings(item_statement);
    }

    sqlite3_finalize(item_statement);

    if (!items_saved) {
        sqlite3_exec(database, "ROLLBACK;", NULL, NULL, NULL);
        return false;
    }

    // Update stock only if there are enough items.
    const char* update_stock_sql =
        "UPDATE items SET stock = stock - ?1 "
        "WHERE id = ?2 AND stock >= ?1;";
    sqlite3_stmt* stock_statement = NULL;
    if (sqlite3_prepare_v2(database, update_stock_sql, -1, &stock_statement, NULL) != SQLITE_OK) {
        sqlite3_exec(database, "ROLLBACK;", NULL, NULL, NULL);
        return false;
    }
    bool stock_updated = true;
    for (int i = 0; i < shop->cart.count; i++) {
        Cart_Item* cart_item = &shop->cart.items[i];
        sqlite3_bind_int(stock_statement, 1, cart_item->quantity);
        sqlite3_bind_int(stock_statement, 2, cart_item->item.id);
        if (cart_item->quantity <= 0 || sqlite3_step(stock_statement) != SQLITE_DONE ||
            sqlite3_changes(database) != 1) {
            stock_updated = false;
            break;
        }
        sqlite3_reset(stock_statement);
        sqlite3_clear_bindings(stock_statement);
    }
    sqlite3_finalize(stock_statement);
    if (!stock_updated) {
        sqlite3_exec(database, "ROLLBACK;", NULL, NULL, NULL);
        return false;
    }

    if (sqlite3_exec(database, "COMMIT;", NULL, NULL, NULL) != SQLITE_OK) {
        sqlite3_exec(database, "ROLLBACK;", NULL, NULL, NULL);
        return false;
    }
    return true;
}
