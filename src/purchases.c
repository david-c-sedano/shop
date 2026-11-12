/* Shows completed purchases for the currently logged-in account. */

static bool purchase_table_exists(Shop* shop, const char* table_name) {
    const char* table_check_sql =
        "SELECT 1 FROM sqlite_master WHERE type = 'table' AND name = ?1 LIMIT 1;";

    sqlite3_stmt* statement = NULL;
    if (sqlite3_prepare_v2(shop->admin.db, table_check_sql, -1, &statement, NULL) != SQLITE_OK) {
        return false;
    }

    sqlite3_bind_text(statement, 1, table_name, -1, SQLITE_TRANSIENT);
    bool table_exists = sqlite3_step(statement) == SQLITE_ROW;
    sqlite3_finalize(statement);
    return table_exists;
}

static void draw_purchase_items(Shop* shop, int order_id) {
    const char* item_sql =
        "SELECT product_name, quantity, unit_price "
        "FROM order_items WHERE order_id = ?1 ORDER BY id;";

    sqlite3_stmt* statement = NULL;
    if (sqlite3_prepare_v2(shop->admin.db, item_sql, -1, &statement, NULL) != SQLITE_OK) {
        ImGui_TextDisabled("Purchase items are unavailable.");
        return;
    }

    sqlite3_bind_int(statement, 1, order_id);

    while (sqlite3_step(statement) == SQLITE_ROW) {
        const unsigned char* product_name = sqlite3_column_text(statement, 0);
        int quantity = sqlite3_column_int(statement, 1);
        double unit_price = sqlite3_column_double(statement, 2);

        char price_text[64];
        format_money(price_text, sizeof(price_text), (float)unit_price);

        ImGui_BulletText("%s x%d at %s",
                         product_name ? (const char*)product_name : "Item",
                         quantity,
                         price_text);
    }

    sqlite3_finalize(statement);
}

void draw_purchase_history(Shop* shop) {
    if (!purchase_table_exists(shop, "orders") || !purchase_table_exists(shop, "order_items")) {
        ImGui_TextDisabled("No past purchases yet.");
        ImGui_TextWrapped("Completed orders will appear here after checkout starts saving purchases.");
        return;
    }

    const char* purchase_sql =
        "SELECT id, subtotal, discount_amount, tax_amount, total_amount, created_at "
        "FROM orders WHERE user_id = ?1 ORDER BY created_at DESC, id DESC;";

    sqlite3_stmt* statement = NULL;
    if (sqlite3_prepare_v2(shop->admin.db, purchase_sql, -1, &statement, NULL) != SQLITE_OK) {
        ImGui_TextDisabled("Purchase history is unavailable.");
        return;
    }

    sqlite3_bind_int(statement, 1, shop->account.user_id);

    bool found_purchase = false;
    while (sqlite3_step(statement) == SQLITE_ROW) {
        found_purchase = true;

        int order_id = sqlite3_column_int(statement, 0);
        double subtotal = sqlite3_column_double(statement, 1);
        double discount = sqlite3_column_double(statement, 2);
        double tax = sqlite3_column_double(statement, 3);
        double total = sqlite3_column_double(statement, 4);
        const unsigned char* created_at = sqlite3_column_text(statement, 5);

        char subtotal_text[64];
        char discount_text[64];
        char tax_text[64];
        char total_text[64];
        format_money(subtotal_text, sizeof(subtotal_text), (float)subtotal);
        format_money(discount_text, sizeof(discount_text), (float)discount);
        format_money(tax_text, sizeof(tax_text), (float)tax);
        format_money(total_text, sizeof(total_text), (float)total);

        ImGui_Text("Order #%d", order_id);
        ImGui_TextDisabled("%s", created_at ? (const char*)created_at : "");
        draw_purchase_items(shop, order_id);
        ImGui_Text("Subtotal: %s", subtotal_text);
        ImGui_Text("Discount: %s", discount_text);
        ImGui_Text("Tax: %s", tax_text);
        ImGui_Text("Total: %s", total_text);
        ImGui_Separator();
    }

    sqlite3_finalize(statement);

    if (!found_purchase) {
        ImGui_TextDisabled("No past purchases yet.");
        ImGui_TextWrapped("Completed orders will appear here after checkout starts saving purchases.");
    }
}
