/* Handles discount code storage and checkout discount calculations. */

bool discount_init(Shop* shop) {
    const char* discount_schema =
        "CREATE TABLE IF NOT EXISTS discount_codes ("
            "id INTEGER PRIMARY KEY AUTOINCREMENT,"
            "code TEXT UNIQUE NOT NULL COLLATE NOCASE,"
            "discount_type TEXT NOT NULL,"
            "discount_value NUMERIC NOT NULL,"
            "is_active INTEGER NOT NULL DEFAULT 1,"
            "expiration_date TEXT,"
            "created_at TEXT DEFAULT CURRENT_TIMESTAMP"
        ");";

    if (sqlite3_exec(shop->admin.db, discount_schema, NULL, NULL, NULL) != SQLITE_OK) {
        return false;
    }

    // Percent codes take a percentage off the subtotal.
    // Fixed codes take a set dollar amount off.
    // Use 1 for active codes and 0 for inactive codes.
    // Use NULL if a coupon has no expiration date.
    const char* starter_discount_sql =
        "INSERT OR IGNORE INTO discount_codes "
        "(code, discount_type, discount_value, is_active, expiration_date) VALUES "
        "('NEWMEMBER20', 'percent', 20, 1, NULL), "
        "('test0', 'fixed', 0.01, 1, NULL), "
        "('test0-', 'percent', 10, 0, NULL), "
        "('NEWYEAR', 'percent', 25, 1, NULL), "
        "('COUPON10', 'percent', 10, 1, NULL);";

    return sqlite3_exec(shop->admin.db, starter_discount_sql, NULL, NULL, NULL) == SQLITE_OK;
}

void discount_reset(Shop* shop) {
    shop->discount_code[0] = '\0';
    shop->discount_box_active = false;
    shop->applied_discount_id = 0;
    shop->applied_discount_amount = 0.0f;
    shop->discount_message[0] = '\0';
}

bool discount_apply(Shop* shop, float subtotal) {
    shop->applied_discount_id = 0;
    shop->applied_discount_amount = 0.0f;

    if (shop->discount_code[0] == '\0') {
        snprintf(shop->discount_message, sizeof(shop->discount_message),
                 "Enter a discount code.");
        return false;
    }

    const char* find_discount_sql =
        "SELECT id, discount_type, discount_value "
        "FROM discount_codes "
        "WHERE code = ?1 COLLATE NOCASE "
        "AND is_active = 1 "
        "AND (expiration_date IS NULL OR expiration_date = '' OR date(expiration_date) >= date('now')) "
        "LIMIT 1;";

    sqlite3_stmt* statement = NULL;
    if (sqlite3_prepare_v2(shop->admin.db, find_discount_sql, -1, &statement, NULL) != SQLITE_OK) {
        snprintf(shop->discount_message, sizeof(shop->discount_message),
                 "Could not check the discount code.");
        return false;
    }

    sqlite3_bind_text(statement, 1, shop->discount_code, -1, SQLITE_TRANSIENT);

    bool discount_found = sqlite3_step(statement) == SQLITE_ROW;
    if (!discount_found) {
        sqlite3_finalize(statement);
        snprintf(shop->discount_message, sizeof(shop->discount_message),
                 "Discount code is invalid or expired.");
        return false;
    }

    int discount_id = sqlite3_column_int(statement, 0);
    const unsigned char* discount_type_text = sqlite3_column_text(statement, 1);
    double discount_value = sqlite3_column_double(statement, 2);
    const char* discount_type = discount_type_text ? (const char*)discount_type_text : "";

    float discount_amount = 0.0f;
    if (strcmp(discount_type, "percent") == 0) {
        discount_amount = subtotal * (float)(discount_value / 100.0);
    } else if (strcmp(discount_type, "fixed") == 0) {
        discount_amount = (float)discount_value;
    } else {
        sqlite3_finalize(statement);
        snprintf(shop->discount_message, sizeof(shop->discount_message),
                 "Discount code has an unsupported type.");
        return false;
    }

    sqlite3_finalize(statement);

    if (discount_amount < 0.0f) {
        discount_amount = 0.0f;
    }
    if (discount_amount > subtotal) {
        discount_amount = subtotal;
    }

    shop->applied_discount_id = discount_id;
    shop->applied_discount_amount = discount_amount;
    snprintf(shop->discount_message, sizeof(shop->discount_message),
             "Discount applied.");
    return true;
}
