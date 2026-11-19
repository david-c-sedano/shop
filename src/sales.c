#include <time.h>
#include <ctype.h>

static bool auth_has_admin_access(Shop* shop) {
    if (!shop_is_admin(shop)) return false;
    sqlite3_stmt* stmt = NULL;
    bool allowed = sqlite3_prepare_v2(shop->admin.db,
        "SELECT 1 FROM users WHERE id = ? AND is_admin = 1 AND is_enabled = 1", -1, &stmt, NULL) == SQLITE_OK &&
        sqlite3_bind_int(stmt, 1, shop->account.user_id) == SQLITE_OK && sqlite3_step(stmt) == SQLITE_ROW;
    sqlite3_finalize(stmt);
    return allowed;
}

static bool sales_error(Shop* shop, const char* message) {
    snprintf(shop->sales.error, sizeof(shop->sales.error), "%s", message);
    return false;
}

static bool sales_exec(Shop* shop, const char* sql) {
    char* error = NULL;
    int rc = sqlite3_exec(shop->admin.db, sql, NULL, NULL, &error);
    if (rc != SQLITE_OK) sales_error(shop, error ? error : "Could not update sales. Try again.");
    sqlite3_free(error);
    return rc == SQLITE_OK;
}

static sqlite3_stmt* sales_prepare(Shop* shop, const char* sql) {
    sqlite3_stmt* stmt = NULL;
    if (sqlite3_prepare_v2(shop->admin.db, sql, -1, &stmt, NULL) != SQLITE_OK) {
        sales_error(shop, "Could not load sales data. Try again.");
    }
    return stmt;
}

static const char* sales_text(sqlite3_stmt* stmt, int column) {
    const char* text = (const char*)sqlite3_column_text(stmt, column);
    return text ? text : "";
}

static double sales_price(double regular, bool percent, double value) {
    return round((percent ? regular * (1.0 - value / 100.0) : value) * 100.0) / 100.0;
}

bool sales_init(Shop* shop) {
    // Migrate existing sale prices once, in the same transaction as the schema marker.
    if (!sales_exec(shop, "BEGIN IMMEDIATE")) return false;
    bool ok = sales_exec(shop,
        "CREATE TABLE IF NOT EXISTS sales ("
        "id INTEGER PRIMARY KEY AUTOINCREMENT, name TEXT NOT NULL,"
        "kind TEXT NOT NULL CHECK(kind IN ('individual','group')),"
        "discount_type TEXT NOT NULL CHECK(discount_type IN ('percent','sale_price')),"
        "discount_value REAL NOT NULL CHECK(discount_value > 0),"
        "starts_at INTEGER NOT NULL, ends_at INTEGER, ended_at INTEGER,"
        "is_paused INTEGER NOT NULL DEFAULT 0 CHECK(is_paused IN (0,1)),"
        "created_at INTEGER NOT NULL DEFAULT (unixepoch()),"
        "CHECK(ends_at IS NULL OR ends_at > starts_at),"
        "CHECK(kind != 'group' OR discount_type = 'percent'),"
        "CHECK(discount_type != 'percent' OR discount_value < 100));"
        "CREATE TABLE IF NOT EXISTS sale_items ("
        "sale_id INTEGER NOT NULL REFERENCES sales(id), item_id INTEGER NOT NULL REFERENCES items(id),"
        "product_name TEXT NOT NULL, regular_price REAL NOT NULL, sale_price REAL NOT NULL,"
        "PRIMARY KEY(sale_id,item_id));"
        "CREATE INDEX IF NOT EXISTS sale_items_product ON sale_items(item_id);"
        "CREATE TABLE IF NOT EXISTS sales_migrations (version INTEGER PRIMARY KEY);"
        "DROP TRIGGER IF EXISTS sale_items_no_overlap;"
        "CREATE TRIGGER sale_items_no_overlap BEFORE INSERT ON sale_items BEGIN "
        "SELECT RAISE(ABORT,'Product already has a sale during these dates.') WHERE EXISTS ("
        "SELECT 1 FROM sale_items si JOIN sales old ON old.id = si.sale_id "
        "JOIN sales new ON new.id = NEW.sale_id WHERE si.item_id = NEW.item_id AND old.id != new.id "
        "AND COALESCE(old.ended_at,old.ends_at,9223372036854775807) > old.starts_at "
        "AND old.starts_at < COALESCE(new.ended_at,new.ends_at,9223372036854775807) "
        "AND new.starts_at < COALESCE(old.ended_at,old.ends_at,9223372036854775807)); END;");
    if (ok) {
        sqlite3_stmt* columns = sales_prepare(shop,"SELECT 1 FROM pragma_table_info('sales') WHERE name='is_paused'");
        bool has_pause = columns && sqlite3_step(columns) == SQLITE_ROW;
        if (!columns) ok = false;
        sqlite3_finalize(columns);
        if (ok && !has_pause) ok = sales_exec(shop,
            "ALTER TABLE sales ADD COLUMN is_paused INTEGER NOT NULL DEFAULT 0 CHECK(is_paused IN (0,1))");
    }
    sqlite3_stmt* check = ok ? sales_prepare(shop, "SELECT 1 FROM sales_migrations WHERE version = 1") : NULL;
    bool migrated = check && sqlite3_step(check) == SQLITE_ROW;
    if (ok && !check) ok = false;
    sqlite3_finalize(check);
    if (ok && !migrated) {
        sqlite3_stmt* items = sales_prepare(shop,
            "SELECT id,display,price,sale_price FROM items WHERE sale_price > 0 AND sale_price < price");
        ok = items != NULL;
        int rc = SQLITE_DONE;
        while (ok && (rc = sqlite3_step(items)) == SQLITE_ROW) {
            char* sql = sqlite3_mprintf(
                "INSERT INTO sales(name,kind,discount_type,discount_value,starts_at) "
                "VALUES(%Q,'individual','sale_price',%!.17g,unixepoch());"
                "INSERT INTO sale_items VALUES(last_insert_rowid(),%d,%Q,%!.17g,%!.17g);",
                sales_text(items, 1), sqlite3_column_double(items, 3), sqlite3_column_int(items, 0),
                sales_text(items, 1), sqlite3_column_double(items, 2), sqlite3_column_double(items, 3));
            ok = sql && sales_exec(shop, sql);
            sqlite3_free(sql);
        }
        if (rc != SQLITE_DONE) ok = false;
        sqlite3_finalize(items);
        if (ok) ok = sales_exec(shop, "INSERT INTO sales_migrations VALUES(1)");
    }
    if (ok) ok = sales_exec(shop, "COMMIT");
    if (!ok) sqlite3_exec(shop->admin.db, "ROLLBACK", NULL, NULL, NULL);
    return ok && sales_refresh(shop);
}

bool sales_refresh(Shop* shop) {
    // Keep the existing sale_price field as the storefront's effective-price cache.
    const char* sql =
        "WITH prices AS (SELECT i.id, COALESCE((SELECT CASE s.discount_type "
        "WHEN 'percent' THEN round(i.price*(1-s.discount_value/100.0),2) ELSE s.discount_value END "
        "FROM sale_items si JOIN sales s ON s.id=si.sale_id WHERE si.item_id=i.id "
        "AND s.starts_at<=unixepoch() AND (s.ends_at IS NULL OR s.ends_at>unixepoch()) "
        "AND s.ended_at IS NULL AND s.is_paused=0 LIMIT 1),0) AS value FROM items i "
        "WHERE EXISTS(SELECT 1 FROM sale_items si WHERE si.item_id=i.id)) "
        "UPDATE items SET sale_price=(SELECT value FROM prices WHERE prices.id=items.id) "
        "WHERE id IN (SELECT id FROM prices) AND "
        "COALESCE(sale_price,0) != (SELECT value FROM prices WHERE prices.id=items.id);";
    if (!sales_exec(shop, sql)) return false;
    sqlite3_stmt* stmt = sales_prepare(shop, "SELECT id,price,sale_price FROM items");
    if (!stmt) return false;
    int rc;
    while ((rc = sqlite3_step(stmt)) == SQLITE_ROW) {
        int id = sqlite3_column_int(stmt, 0);
        Item_List* lists[] = {&shop->display, &shop->featured};
        for (int l = 0; l < 2; l++) {
            for (int i = 0; i < lists[l]->count; i++) {
                Item* item = &lists[l]->items[i];
                if (item->id == id) {
                    item->price = sqlite3_column_double(stmt, 1);
                    item->sale_price = sqlite3_column_double(stmt, 2);
                }
            }
        }
        for (int i = 0; i < shop->cart.count; i++) {
            if (shop->cart.items[i].item.id == id) {
                shop->cart.items[i].item.price = sqlite3_column_double(stmt, 1);
                shop->cart.items[i].item.sale_price = sqlite3_column_double(stmt, 2);
            }
        }
    }
    sqlite3_finalize(stmt);
    if (rc != SQLITE_DONE) return sales_error(shop, "Could not refresh sale prices. Try again.");
    if (shop->applied_discount_id) discount_apply(shop, cart_subtotal(shop));
    shop->sales.refreshed_at = (sqlite3_int64)time(NULL);
    return true;
}

bool sales_checkout_matches(Shop* shop, int code_id, float subtotal, float discount) {
    if (!sales_refresh(shop)) return false;
    float current_subtotal = cart_subtotal(shop);
    if (current_subtotal != subtotal) return false;
    if (code_id) {
        if (!discount_apply(shop,current_subtotal) || shop->applied_discount_id != code_id ||
            shop->applied_discount_amount != discount) return false;
    } else if (discount != 0) {
        return false;
    }
    return true;
}

static bool sales_parse_local(const char* text, bool date_only, sqlite3_int64* result) {
    int year, month, day, hour = 0, minute = 0, used = 0;
    int count = date_only ? sscanf(text, "%4d-%2d-%2d%n", &year, &month, &day, &used)
                         : sscanf(text, "%4d-%2d-%2d %2d:%2d%n", &year, &month, &day, &hour, &minute, &used);
    if (strlen(text) != (date_only ? 10 : 16) || text[4] != '-' || text[7] != '-' ||
        (!date_only && (text[10] != ' ' || text[13] != ':')) || count != (date_only ? 3 : 5) || text[used] || year < 1970 || year > 9999 ||
        month < 1 || month > 12 || day < 1 || day > 31 || hour < 0 || hour > 23 || minute < 0 || minute > 59) return false;
    struct tm local = {0};
    local.tm_year = year - 1900;
    local.tm_mon = month - 1;
    local.tm_mday = day;
    local.tm_hour = hour;
    local.tm_min = minute;
    local.tm_isdst = -1;
    time_t stamp = mktime(&local);
    if (stamp == (time_t)-1 || local.tm_year != year-1900 || local.tm_mon != month-1 ||
        local.tm_mday != day || local.tm_hour != hour || local.tm_min != minute) return false;
    *result = (sqlite3_int64)stamp;
    return true;
}

static void sales_format_time(sqlite3_int64 stamp, char* output, size_t size) {
    if (!stamp) { snprintf(output, size, "No End Date"); return; }
    time_t t = (time_t)stamp;
    struct tm* local = localtime(&t);
    if (!local || !strftime(output, size, "%Y-%m-%d %H:%M", local)) snprintf(output, size, "Invalid date");
}

static bool sales_window(Shop* shop, sqlite3_int64* start, sqlite3_int64* end) {
    Sales_State* state = &shop->sales;
    *start = state->edit_started ? state->original_start : (sqlite3_int64)time(NULL);
    *end = 0;
    if (!state->edit_started && state->scheduled && (!sales_parse_local(state->starts, false, start) || *start <= (sqlite3_int64)time(NULL)))
        return sales_error(shop, "Enter a future start as YYYY-MM-DD HH:MM.");
    if (state->scheduled || state->has_end) {
        if (!sales_parse_local(state->ends, false, end) || *end <= *start || *end <= (sqlite3_int64)time(NULL))
            return sales_error(shop, "Enter an end after the start as YYYY-MM-DD HH:MM.");
    }
    return true;
}

// Caller owns a transaction so product edits and sale changes commit together.
static bool sales_write(Shop* shop, const char* name, bool group, bool percent, double value,
                         sqlite3_int64 start, sqlite3_int64 end, const int* ids, int count, sqlite3_int64 editing_id) {
    if (!auth_has_admin_access(shop)) return sales_error(shop, "Sign in as an admin to manage sales.");
    if (!count || !isfinite(value) || value <= 0 || (percent && value >= 100) ||
        (group && !percent) || (!group && count != 1))
        return sales_error(shop, "Select products and enter a valid discount below 100%.");
    if (name[strspn(name, " \r\n\t")] == '\0') return sales_error(shop, "Enter a sale name.");
    if (end && end <= start) return sales_error(shop, "The end must be after the start.");
    sqlite3_stmt* stmt = sales_prepare(shop,
        editing_id ? "UPDATE sales SET name=?,kind=?,discount_type=?,discount_value=?,starts_at=?,ends_at=? "
        "WHERE id=? AND ended_at IS NULL AND (ends_at IS NULL OR ends_at>unixepoch())" :
        "INSERT INTO sales(name,kind,discount_type,discount_value,starts_at,ends_at) VALUES(?,?,?,?,?,?)");
    if (!stmt) return false;
    sqlite3_bind_text(stmt, 1, name, -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 2, group ? "group" : "individual", -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 3, percent ? "percent" : "sale_price", -1, SQLITE_STATIC);
    sqlite3_bind_double(stmt, 4, value);
    sqlite3_bind_int64(stmt, 5, start);
    if (end) sqlite3_bind_int64(stmt, 6, end); else sqlite3_bind_null(stmt, 6);
    if (editing_id) sqlite3_bind_int64(stmt,7,editing_id);
    bool ok = sqlite3_step(stmt) == SQLITE_DONE && sqlite3_changes(shop->admin.db) == 1;
    sqlite3_finalize(stmt);
    if (!ok) return sales_error(shop, "The sale ended or could not be saved. Reopen it and try again.");
    sqlite3_int64 sale_id = editing_id ? editing_id : sqlite3_last_insert_rowid(shop->admin.db);
    if (editing_id) {
        // Remove the old links only inside the transaction, so conflicts restore them on rollback.
        stmt = sales_prepare(shop,"UPDATE items SET sale_price=0 WHERE id IN (SELECT item_id FROM sale_items WHERE sale_id=?)");
        if (!stmt) return false;
        sqlite3_bind_int64(stmt,1,editing_id);
        ok = sqlite3_step(stmt) == SQLITE_DONE;
        sqlite3_finalize(stmt);
        stmt = sales_prepare(shop,"DELETE FROM sale_items WHERE sale_id=?");
        if (!stmt) return false;
        sqlite3_bind_int64(stmt,1,editing_id);
        ok = ok && sqlite3_step(stmt) == SQLITE_DONE;
        sqlite3_finalize(stmt);
        if (!ok) return sales_error(shop,"Could not update the sale's products.");
    }
    for (int i = 0; i < count; i++) {
        stmt = sales_prepare(shop, "SELECT display,price FROM items WHERE id=?");
        if (!stmt) return false;
        sqlite3_bind_int(stmt, 1, ids[i]);
        bool found = sqlite3_step(stmt) == SQLITE_ROW;
        double regular = found ? sqlite3_column_double(stmt, 1) : 0;
        double price = sales_price(regular, percent, value);
        char product[100];
        snprintf(product, sizeof(product), "%s", found ? sales_text(stmt, 0) : "");
        sqlite3_finalize(stmt);
        if (!found || !isfinite(regular) || price < 0.01 || price >= regular)
            return sales_error(shop, "Each selected product must have a sale price of at least $0.01 below its regular price.");
        stmt = sales_prepare(shop, "INSERT INTO sale_items VALUES(?,?,?,?,?)");
        if (!stmt) return false;
        sqlite3_bind_int64(stmt, 1, sale_id);
        sqlite3_bind_int(stmt, 2, ids[i]);
        sqlite3_bind_text(stmt, 3, product, -1, SQLITE_TRANSIENT);
        sqlite3_bind_double(stmt, 4, regular);
        sqlite3_bind_double(stmt, 5, price);
        ok = sqlite3_step(stmt) == SQLITE_DONE;
        if (!ok) sales_error(shop, sqlite3_errmsg(shop->admin.db));
        sqlite3_finalize(stmt);
        if (!ok) return false;
    }
    return true;
}

static bool sales_insert(Shop* shop, const char* name, bool group, bool percent, double value,
                         sqlite3_int64 start, sqlite3_int64 end, const int* ids, int count) {
    return sales_write(shop,name,group,percent,value,start,end,ids,count,0);
}

static bool sales_finish_transaction(Shop* shop, bool ok) {
    if (ok) ok = sales_refresh(shop) && sales_exec(shop, "RELEASE sales_edit");
    if (!ok) {
        sqlite3_exec(shop->admin.db, "ROLLBACK TO sales_edit; RELEASE sales_edit;", NULL, NULL, NULL);
        // Reload caches after a failed commit as well.
        sales_refresh(shop);
    }
    return ok;
}

static bool sales_end(Shop* shop, sqlite3_int64 id) {
    if (!auth_has_admin_access(shop)) return sales_error(shop, "Sign in as an admin to end sales.");
    if (!sales_exec(shop, "SAVEPOINT sales_edit")) return false;
    sqlite3_stmt* stmt = sales_prepare(shop,
        "UPDATE sales SET ended_at=unixepoch() WHERE id=? AND ended_at IS NULL "
        "AND (ends_at IS NULL OR ends_at>unixepoch())");
    bool ok = stmt != NULL;
    if (stmt) {
        sqlite3_bind_int64(stmt, 1, id);
        ok = sqlite3_step(stmt) == SQLITE_DONE && sqlite3_changes(shop->admin.db) == 1;
        sqlite3_finalize(stmt);
    }
    if (!ok) sales_error(shop, "This sale already ended or is no longer available.");
    return sales_finish_transaction(shop, ok);
}

static bool sales_set_paused(Shop* shop, sqlite3_int64 id, bool paused) {
    if (!auth_has_admin_access(shop)) return sales_error(shop,"Sign in as an admin to pause or resume sales.");
    if (!sales_exec(shop,"SAVEPOINT sales_edit")) return false;
    sqlite3_stmt* stmt = sales_prepare(shop,
        "UPDATE sales SET is_paused=? WHERE id=? AND ended_at IS NULL AND starts_at<=unixepoch() "
        "AND (ends_at IS NULL OR ends_at>unixepoch())");
    bool ok = stmt != NULL;
    if (stmt) {
        sqlite3_bind_int(stmt,1,paused);
        sqlite3_bind_int64(stmt,2,id);
        ok = sqlite3_step(stmt) == SQLITE_DONE && sqlite3_changes(shop->admin.db) == 1;
        sqlite3_finalize(stmt);
    }
    if (!ok) sales_error(shop,"This sale already ended or is no longer ongoing.");
    return sales_finish_transaction(shop,ok);
}

static int sales_month_days(int year, int month);

static bool sales_expiration_valid(const char* text) {
    if (!*text) return true;
    int year, month, day, hour = 0, minute = 0, used = 0;
    bool timed = strlen(text) == 16;
    int fields = timed ? sscanf(text,"%4d-%2d-%2d %2d:%2d%n",&year,&month,&day,&hour,&minute,&used)
                       : sscanf(text,"%4d-%2d-%2d%n",&year,&month,&day,&used);
    if (strlen(text) != (timed ? 16 : 10) || fields != (timed ? 5 : 3) || text[used] ||
        text[4] != '-' || text[7] != '-' || (timed && (text[10] != ' ' || text[13] != ':')) ||
        year < 1970 || year > 9999 || month < 1 || month > 12 || day < 1 ||
        hour < 0 || hour > 23 || minute < 0 || minute > 59) return false;
    for (int i = 0; text[i]; i++)
        if (i != 4 && i != 7 && i != 10 && i != 13 && !isdigit((unsigned char)text[i])) return false;
    return day <= sales_month_days(year,month);
}

static bool sales_create_code(Shop* shop) {
    Sales_State* state = &shop->sales;
    if (!auth_has_admin_access(shop)) return sales_error(shop, "Sign in as an admin to create codes.");
    size_t length = strlen(state->name);
    if (!length || length >= sizeof(shop->discount_code)) return sales_error(shop, "Use a code between 1 and 31 characters.");
    for (size_t i = 0; i < length; i++) {
        unsigned char c = (unsigned char)state->name[i];
        if (!isalnum(c) && c != '-' && c != '_') return sales_error(shop, "Codes may contain letters, numbers, hyphens, and underscores.");
    }
    if (!isfinite(state->value) || state->value <= 0 || (state->percent && state->value > 100) ||
        (!state->percent && round(state->value*100)/100 < 0.01))
        return sales_error(shop, "Enter a positive discount, up to 100% for percentage codes.");
    if (!sales_expiration_valid(state->expires))
        return sales_error(shop,"Choose a valid UTC expiration date and time, or leave it blank.");
    sqlite3_stmt* stmt = sales_prepare(shop,
        state->editing_id ?
        "UPDATE discount_codes SET code=?,discount_type=?,discount_value=?,is_active=?,expiration_date=? "
        "WHERE id=?7 AND deleted_at IS NULL AND NOT EXISTS(SELECT 1 FROM discount_codes WHERE code=?6 COLLATE NOCASE AND id!=?7)" :
        "INSERT INTO discount_codes(code,discount_type,discount_value,is_active,expiration_date) "
        "SELECT ?,?,?,?,? WHERE NOT EXISTS (SELECT 1 FROM discount_codes WHERE code=? COLLATE NOCASE)");
    if (!stmt) return false;
    sqlite3_bind_text(stmt, 1, state->name, -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 2, state->percent ? "percent" : "fixed", -1, SQLITE_STATIC);
    sqlite3_bind_double(stmt, 3, state->percent ? state->value : round(state->value*100)/100);
    sqlite3_bind_int(stmt, 4, state->enabled);
    if (state->expires[0]) sqlite3_bind_text(stmt, 5, state->expires, -1, SQLITE_STATIC); else sqlite3_bind_null(stmt, 5);
    sqlite3_bind_text(stmt, 6, state->name, -1, SQLITE_STATIC);
    if (state->editing_id) sqlite3_bind_int64(stmt,7,state->editing_id);
    bool ok = sqlite3_step(stmt) == SQLITE_DONE && sqlite3_changes(shop->admin.db) == 1;
    sqlite3_finalize(stmt);
    if (!ok) return sales_error(shop, "That code already exists or could not be saved.");
    return true;
}

static bool sales_delete_code(Shop* shop, sqlite3_int64 id) {
    if (!auth_has_admin_access(shop)) return sales_error(shop,"Sign in as an admin to delete codes.");
    if (!sales_exec(shop,"SAVEPOINT sales_edit")) return false;
    // Retain the referenced record for historical orders and to prevent starter-code resurrection.
    sqlite3_stmt* stmt = sales_prepare(shop,
        "UPDATE discount_codes SET deleted_at=CURRENT_TIMESTAMP,is_active=0 WHERE id=? AND deleted_at IS NULL "
        "AND EXISTS(SELECT 1 FROM users WHERE id=? AND is_admin=1)");
    bool ok = stmt != NULL;
    if (stmt) {
        sqlite3_bind_int64(stmt,1,id);
        sqlite3_bind_int(stmt,2,shop->account.user_id);
        ok = sqlite3_step(stmt) == SQLITE_DONE && sqlite3_changes(shop->admin.db) == 1;
        sqlite3_finalize(stmt);
    }
    if (!ok) sales_error(shop,"This code was already deleted or could not be deleted.");
    return sales_finish_transaction(shop,ok);
}

static bool sales_editor_update(Shop* shop, Item item, bool on_sale, double original_sale_price) {
    bool changed = on_sale != (original_sale_price > 0) || (on_sale && item.sale_price != original_sale_price);
    sqlite3_stmt* stmt = sales_prepare(shop,
        "SELECT s.id,s.kind,s.discount_type,s.discount_value FROM sales s JOIN sale_items si ON si.sale_id=s.id "
        "WHERE si.item_id=? AND s.starts_at<=unixepoch() AND (s.ends_at IS NULL OR s.ends_at>unixepoch()) AND s.ended_at IS NULL");
    if (!stmt) return false;
    sqlite3_bind_int(stmt, 1, item.id);
    bool found = sqlite3_step(stmt) == SQLITE_ROW;
    sqlite3_int64 id = found ? sqlite3_column_int64(stmt, 0) : 0;
    bool group = found && strcmp(sales_text(stmt, 1), "group") == 0;
    bool percent = found && strcmp(sales_text(stmt, 2), "percent") == 0;
    double value = found ? sqlite3_column_double(stmt, 3) : 0;
    sqlite3_finalize(stmt);
    if (group && changed) return sales_error(shop, "Manage this product's group sale from Sales.");
    if (found && !changed) {
        double price = sales_price(item.price, percent, value);
        if (price < 0.01 || price >= item.price) return sales_error(shop, "This regular price conflicts with the current sale. End the sale first.");
        return true;
    }
    if (!changed) return true;
    if (found) {
        stmt = sales_prepare(shop, "UPDATE sales SET ended_at=unixepoch() WHERE id=?");
        if (!stmt) return false;
        sqlite3_bind_int64(stmt, 1, id);
        bool ok = sqlite3_step(stmt) == SQLITE_DONE;
        sqlite3_finalize(stmt);
        if (!ok) return false;
    }
    if (on_sale) return sales_insert(shop, item.display, false, false, item.sale_price,
                                    (sqlite3_int64)time(NULL), 0, &item.id, 1);
    return true;
}

void sales_close_form(Shop* shop) {
    arena_free(&shop->sales.products.alloc);
    shop->sales.products = (Item_List){0};
    free(shop->sales.selected);
    shop->sales.selected = NULL;
    shop->sales.form = 0;
    shop->sales.editing_id = 0;
    shop->sales.edit_past = shop->sales.edit_started = shop->sales.confirm_delete = false;
    shop->sales.start_locked_notice = false;
    shop->sales.confirm_end = 0;
    shop->sales.open_end_confirmation = false;
    shop->sales.advanced_open = false;
}

static void sales_open_form(Shop* shop, int form) {
    sales_close_form(shop);
    Sales_State* state = &shop->sales;
    state->form = form;
    state->percent = true;
    state->enabled = true;
    state->scheduled = state->has_end = false;
    state->value = 20;
    state->name[0] = state->search[0] = state->expires[0] = state->error[0] = state->message[0] = '\0';
    sales_format_time((sqlite3_int64)time(NULL)+3600, state->starts, sizeof(state->starts));
    sales_format_time((sqlite3_int64)time(NULL)+86400, state->ends, sizeof(state->ends));
    if (form != 1) {
        state->products = query_items(shop, "SELECT " ITEM_COLUMNS " FROM items ORDER BY display COLLATE NOCASE,id");
        state->selected = calloc(MAX(1, state->products.count), sizeof(bool));
        if (!state->selected) {
            sales_close_form(shop);
            sales_error(shop, "Not enough memory to open the sale form.");
        }
    }
}

static void sales_open_edit(Shop* shop, int form, sqlite3_int64 id) {
    if (!auth_has_admin_access(shop)) return;
    sales_open_form(shop,form);
    Sales_State* state = &shop->sales;
    if (!state->form) return;
    sqlite3_stmt* stmt = sales_prepare(shop,form == 1 ?
        "SELECT code,discount_type,discount_value,is_active,COALESCE(expiration_date,'') FROM discount_codes WHERE id=? AND deleted_at IS NULL" :
        "SELECT name,discount_type,discount_value,starts_at,COALESCE(ends_at,0),"
        "(ended_at IS NOT NULL OR ends_at<=unixepoch()),starts_at<=unixepoch() FROM sales WHERE id=?");
    if (!stmt) { sales_close_form(shop);return; }
    sqlite3_bind_int64(stmt,1,id);
    if (sqlite3_step(stmt) != SQLITE_ROW) {
        sqlite3_finalize(stmt);sales_close_form(shop);sales_error(shop,"This entry is no longer available.");return;
    }
    state->editing_id = id;
    snprintf(state->name,sizeof(state->name),"%s",sales_text(stmt,0));
    state->percent = strcmp(sales_text(stmt,1),"percent") == 0;
    state->value = sqlite3_column_double(stmt,2);
    if (form == 1) {
        state->enabled = sqlite3_column_int(stmt,3) != 0;
        snprintf(state->expires,sizeof(state->expires),"%s",sales_text(stmt,4));
    } else {
        state->original_start = sqlite3_column_int64(stmt,3);
        state->has_end = sqlite3_column_int64(stmt,4) != 0;
        state->edit_past = sqlite3_column_int(stmt,5) != 0;
        state->edit_started = sqlite3_column_int(stmt,6) != 0;
        state->scheduled = !state->edit_started;
        sales_format_time(state->original_start,state->starts,sizeof(state->starts));
        sales_format_time(sqlite3_column_int64(stmt,4),state->ends,sizeof(state->ends));
    }
    sqlite3_finalize(stmt);
    if (form != 1) {
        stmt = sales_prepare(shop,"SELECT item_id FROM sale_items WHERE sale_id=?");
        if (!stmt) return;
        sqlite3_bind_int64(stmt,1,id);
        while (sqlite3_step(stmt) == SQLITE_ROW) {
            for (int i = 0; i < state->products.count; i++)
                if (state->products.items[i].id == sqlite3_column_int(stmt,0)) state->selected[i] = true;
        }
        sqlite3_finalize(stmt);
    }
}

static bool sales_save_form(Shop* shop) {
    Sales_State* state = &shop->sales;
    state->error[0] = '\0';
    if (state->form == 1) return sales_create_code(shop);
    if (state->edit_past) {
        if (!auth_has_admin_access(shop)) return sales_error(shop,"Sign in as an admin to edit sales.");
        if (!state->name[strspn(state->name," \t\r\n")]) return sales_error(shop,"Enter a sale name.");
        sqlite3_stmt* stmt = sales_prepare(shop,"UPDATE sales SET name=? WHERE id=?");
        if (!stmt) return false;
        sqlite3_bind_text(stmt,1,state->name,-1,SQLITE_STATIC);
        sqlite3_bind_int64(stmt,2,state->editing_id);
        bool ok = sqlite3_step(stmt) == SQLITE_DONE && sqlite3_changes(shop->admin.db) == 1;
        sqlite3_finalize(stmt);
        return ok || sales_error(shop,"Could not update the sale name.");
    }
    sqlite3_int64 start, end;
    if (!sales_window(shop, &start, &end)) return false;
    int* ids = malloc(MAX(1, state->products.count)*sizeof(int));
    if (!ids) return sales_error(shop, "Not enough memory to save this sale.");
    int count = 0;
    const char* name = state->name;
    for (int i = 0; i < state->products.count; i++) {
        if (state->selected[i]) {
            ids[count++] = state->products.items[i].id;
            if (state->form == 2) name = state->products.items[i].display;
        }
    }
    bool ok = sales_exec(shop, "SAVEPOINT sales_edit");
    if (ok) {
        ok = sales_write(shop, name, state->form == 3, state->percent, state->value, start, end, ids, count,state->editing_id);
        ok = sales_finish_transaction(shop, ok);
    }
    free(ids);
    return ok;
}

static bool sales_matches(const char* text, const char* search) {
    if (!*search) return true;
    for (; *text; text++) {
        const char* t = text;
        const char* s = search;
        while (*s && *t && tolower((unsigned char)*t) == tolower((unsigned char)*s)) { t++; s++; }
        if (!*s) return true;
    }
    return false;
}

static void sales_field(const char* label, const char* id, char* buffer, size_t size, const char* hint) {
    ImGui_TextDisabled("%s", label);
    ImGui_SetNextItemWidth(-FLT_MIN);
    ImGui_InputTextWithHint(id, hint, buffer, size, 0);
}

static void sales_page_color(ImGuiCol slot, Color color);

static void sales_delete_confirmation(Shop* shop) {
    Sales_State* state = &shop->sales;
    ImVec2 view = ImGui_GetIO()->DisplaySize;
    ImGui_SetNextWindowPosEx((ImVec2){view.x/2,view.y/2},ImGuiCond_Appearing,(ImVec2){0.5f,0.5f});
    ImGui_SetNextWindowSizeConstraints((ImVec2){fminf(420,view.x-24),0},(ImVec2){fminf(520,view.x-24),view.y-24},NULL,NULL);
    if (panel_begin_modal("Delete Code",ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoSavedSettings)) {
        ImGui_TextWrapped("Are you sure you want to delete %s?",state->delete_name);
        if (state->error[0]) ImGui_TextWrapped("%s",state->error);
        float width = 220+ImGui_GetStyle()->ItemSpacing.x;
        ImGui_SetCursorPosX(ImGui_GetCursorPosX()+fmaxf(0,(ImGui_GetContentRegionAvail().x-width)/2));
        sales_page_color(ImGuiCol_Button,SHOP_RED);
        sales_page_color(ImGuiCol_ButtonHovered,ColorBrightness(SHOP_RED,-0.5f));
        sales_page_color(ImGuiCol_ButtonActive,ColorBrightness(SHOP_RED,-0.8f));
        sales_page_color(ImGuiCol_Text,BLACK);
        bool remove = ImGui_ButtonEx("Delete Code",(ImVec2){130,32});
        ImGui_PopStyleColorEx(4);
        ImGui_SameLine();
        bool cancel = ImGui_ButtonEx("Cancel",(ImVec2){90,32});
        bool deleted = remove && sales_delete_code(shop,state->editing_id);
        if (deleted || cancel) {
            state->confirm_delete = false;
            ImGui_CloseCurrentPopup();
        }
        ImGui_EndPopup();
        if (deleted) {
            sales_close_form(shop);
            snprintf(state->message,sizeof(state->message),"Discount code deleted.");
            memset(state->pages,0,sizeof(state->pages));
        }
    }
}

static void sales_end_confirmation(Shop* shop);
static void sales_date_picker(const char* label, const char* id, char* date, size_t capacity, bool with_time, bool utc);

static void sales_form_panel(Shop* shop) {
    Sales_State* state = &shop->sales;
    const char* title = state->editing_id ? (state->form == 1 ? "Edit Discount Code" : state->form == 2 ? "Edit Item Sale" : "Edit Group Sale") :
        state->form == 1 ? "Create Discount Code" : state->form == 2 ? "Put Item on Sale" : "Create Group Sale";
    ImVec2 view = ImGui_GetIO()->DisplaySize;
    ImGui_SetNextWindowPosEx((ImVec2){view.x/2,view.y/2}, ImGuiCond_Always, (ImVec2){0.5f,0.5f});
    float panel_width = fminf(590,view.x-24);
    ImGui_SetNextWindowSize((ImVec2){panel_width,0}, ImGuiCond_Always);
    ImGui_SetNextWindowSizeConstraints((ImVec2){panel_width,0},(ImVec2){panel_width,view.y-24},NULL,NULL);
    ImGui_PushStyleVarImVec2(ImGuiStyleVar_WindowPadding, (ImVec2){20,16});
    ImGui_PushStyleVarImVec2(ImGuiStyleVar_ItemSpacing, (ImVec2){12,10});
    if (ImGui_Begin(title, NULL, ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoSavedSettings)) {
        if (state->form != 2 || state->edit_past) sales_field(state->form == 1 ? "Code" : "Sale Name", "##sale_name", state->name, sizeof(state->name), "");
        if (state->edit_past) ImGui_TextWrapped("Past sales keep their historical products, prices, and dates. You can update the name.");
        ImGui_BeginDisabled(state->edit_past);
        if (state->form != 1) {
            sales_field("Find Products", "##sale_search", state->search, sizeof(state->search), "Product Name or Category");
            int selected_count = 0;
            for (int i = 0; i < state->products.count; i++) selected_count += state->selected[i];
            ImGui_Text("Products (%d selected)", selected_count);
            sales_page_color(ImGuiCol_ScrollbarBg,SHOP_SHADOW);
            sales_page_color(ImGuiCol_ScrollbarGrab,SHOP_TEXT_MUTED);
            sales_page_color(ImGuiCol_ScrollbarGrabHovered,ColorBrightness(SHOP_TEXT_MUTED,0.15f));
            sales_page_color(ImGuiCol_ScrollbarGrabActive,ColorBrightness(SHOP_TEXT_MUTED,-0.15f));
            ImGui_PushStyleVar(ImGuiStyleVar_ScrollbarSize,12);
            ImGui_PushStyleVar(ImGuiStyleVar_ScrollbarRounding,0);
            ImGui_PushStyleVar(ImGuiStyleVar_GrabMinSize,24);
            if (ImGui_BeginChild("##sale_products", (ImVec2){0,156}, ImGuiChildFlags_Borders, ImGuiWindowFlags_NoScrollWithMouse)) {
                float wheel = ImGui_GetIO()->MouseWheel;
                if (wheel != 0 && ImGui_IsWindowHovered(ImGuiHoveredFlags_ChildWindows))
                    ImGui_SetScrollY(Clamp(ImGui_GetScrollY()-wheel*ImGui_GetTextLineHeightWithSpacing()*3,0,ImGui_GetScrollMaxY()));
                if (ImGui_BeginTable("##product_choices",2,ImGuiTableFlags_SizingStretchSame)) {
                    for (int i = 0; i < state->products.count; i++) {
                        Item item = state->products.items[i];
                        if (!sales_matches(item.display, state->search) && !sales_matches(item.category, state->search)) continue;
                        ImGui_TableNextColumn();
                        ImGui_PushIDInt(item.id);
                        bool selected = state->selected[i];
                        bool changed = false;
                        char price[128];
                        format_money(price,sizeof(price),item.price);
                        if (ImGui_BeginTable("##product_row",3,ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_NoSavedSettings)) {
                            ImGui_TableSetupColumnEx("##check",ImGuiTableColumnFlags_WidthFixed,ImGui_GetFrameHeight(),0);
                            ImGui_TableSetupColumnEx("##name",ImGuiTableColumnFlags_WidthStretch,1,0);
                            ImGui_TableSetupColumnEx("##price",ImGuiTableColumnFlags_WidthFixed,ImGui_CalcTextSize(price).x,0);
                            ImGui_TableNextColumn();
                            changed = ImGui_Checkbox("##selected", &selected);
                            ImGui_TableNextColumn();
                            ImGui_TextWrapped("%s",item.display);
                            if (ImGui_IsItemClicked()) { selected = !selected; changed = true; }
                            ImGui_TableNextColumn();
                            ImGui_SetCursorPosX(ImGui_GetCursorPosX()+fmaxf(0,ImGui_GetContentRegionAvail().x-ImGui_CalcTextSize(price).x));
                            ImGui_TextUnformatted(price);
                            if (ImGui_IsItemClicked()) { selected = !selected; changed = true; }
                            ImGui_EndTable();
                        }
                        if (changed) {
                            if (state->form == 2) memset(state->selected, 0, state->products.count*sizeof(bool));
                            state->selected[i] = selected;
                        }
                        ImGui_PopID();
                    }
                    ImGui_EndTable();
                }
                if (!state->products.count) ImGui_TextDisabled("No products available.");
            }
            ImGui_EndChild();
            ImGui_PopStyleVarEx(3);
            ImGui_PopStyleColorEx(4);
        }
        if (state->form != 3) {
            bool percent = state->percent;
            if (ImGui_Checkbox("Percentage Off",&percent) && percent) state->percent = true;
            ImGui_SameLine();
            bool fixed = !state->percent;
            if (ImGui_Checkbox(state->form == 1 ? "Dollar Amount Off" : "Sale Price",&fixed) && fixed) state->percent = false;
        }
        ImGui_TextDisabled("%s", state->percent ? "Discount (%)" : state->form == 1 ? "Discount ($)" : "Sale Price ($)");
        ImGui_SetNextItemWidth(-FLT_MIN);
        ImGui_InputDoubleEx("##sale_value", &state->value, 0,0,"%.2f",0);
        if (state->form == 2) {
            for (int i = 0; i < state->products.count; i++) {
                if (!state->selected[i]) continue;
                Item item = state->products.items[i];
                double price = sales_price(item.price, state->percent, state->value);
                ImGui_Text("Regular $%.2f   Sale $%.2f   %.2f%% off", item.price, price,
                           item.price > 0 ? (1-price/item.price)*100 : 0);
            }
        }
        if (state->form == 1) {
            ImGui_Checkbox("Active", &state->enabled);
            sales_date_picker("Expiration (Optional, UTC)", "##code_expires", state->expires, sizeof(state->expires), true, true);
        } else {
            ImGui_BeginDisabled(state->edit_started);
            bool now = !state->scheduled;
            if (ImGui_Checkbox("Start Now",&now) && now) state->scheduled = false;
            bool locked_click = ImGui_IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled) && ImGui_IsMouseClicked(0);
            ImGui_SameLine();
            bool scheduled = state->scheduled;
            if (ImGui_Checkbox("Schedule",&scheduled) && scheduled) state->scheduled = true;
            locked_click |= ImGui_IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled) && ImGui_IsMouseClicked(0);
            ImGui_EndDisabled();
            if (state->edit_started && locked_click) state->start_locked_notice = true;
            if (state->start_locked_notice)
                ImGui_TextWrapped("This sale has already started, so Start Now and Schedule cannot be changed.");
            ImGui_TextDisabled("Dates and times use this computer's local time.");
            if (state->scheduled) sales_date_picker("Starts", "##sale_starts", state->starts, sizeof(state->starts), true, false);
            if (!state->scheduled) ImGui_Checkbox("Set End Date and Time", &state->has_end);
            if (state->scheduled || state->has_end) sales_date_picker("Ends", "##sale_ends", state->ends, sizeof(state->ends), true, false);
        }
        ImGui_EndDisabled();
        if (state->error[0]) ImGui_TextWrapped("%s", state->error);
        ImGui_Separator();
        ImGui_PushStyleVarImVec2(ImGuiStyleVar_ItemSpacing,(ImVec2){16,8});
        ImGui_Dummy((ImVec2){0,4});
        bool can_delete = state->form == 1 && state->editing_id != 0;
        bool can_end = state->form != 1 && state->editing_id != 0 && !state->edit_past;
        float width = 220 + ImGui_GetStyle()->ItemSpacing.x;
        if (can_delete || can_end) width += 110+ImGui_GetStyle()->ItemSpacing.x;
        ImGui_SetCursorPosX(ImGui_GetCursorPosX()+fmaxf(0,(ImGui_GetContentRegionAvail().x-width)/2));
        bool save = ImGui_ButtonEx(state->editing_id ? "Save Changes" : state->form == 1 ? "Create Code" : "Create Sale", (ImVec2){130,32});
        ImGui_SameLine();
        bool cancel = ImGui_ButtonEx("Cancel", (ImVec2){90,32});
        if (can_delete) {
            ImGui_SameLine();
            sales_page_color(ImGuiCol_Button,SHOP_RED);
            sales_page_color(ImGuiCol_ButtonHovered,ColorBrightness(SHOP_RED,-0.5f));
            sales_page_color(ImGuiCol_ButtonActive,ColorBrightness(SHOP_RED,-0.8f));
            sales_page_color(ImGuiCol_Text,BLACK);
            bool remove = ImGui_ButtonEx("Delete Code",(ImVec2){110,32});
            ImGui_PopStyleColorEx(4);
            if (remove) {
                sqlite3_stmt* code = sales_prepare(shop,"SELECT code FROM discount_codes WHERE id=? AND deleted_at IS NULL");
                if (code) {
                    sqlite3_bind_int64(code,1,state->editing_id);
                    if (sqlite3_step(code) == SQLITE_ROW) {
                        snprintf(state->delete_name,sizeof(state->delete_name),"%s",sales_text(code,0));
                        state->confirm_delete = true;
                        state->error[0] = '\0';
                        ImGui_OpenPopup("Delete Code",0);
                    }
                    sqlite3_finalize(code);
                }
            }
        }
        if (can_end) {
            ImGui_SameLine();
            sales_page_color(ImGuiCol_Button,SHOP_RED);
            sales_page_color(ImGuiCol_ButtonHovered,ColorBrightness(SHOP_RED,-0.5f));
            sales_page_color(ImGuiCol_ButtonActive,ColorBrightness(SHOP_RED,-0.8f));
            sales_page_color(ImGuiCol_Text,BLACK);
            bool end = ImGui_ButtonEx(state->edit_started ? "End Sale" : "Cancel Sale",(ImVec2){110,32});
            ImGui_PopStyleColorEx(4);
            if (end) {
                sqlite3_stmt* sale = sales_prepare(shop,"SELECT name FROM sales WHERE id=?");
                if (sale) {
                    sqlite3_bind_int64(sale,1,state->editing_id);
                    if (sqlite3_step(sale) == SQLITE_ROW) {
                        state->confirm_end = state->editing_id;
                        state->open_end_confirmation = true;
                        snprintf(state->end_name,sizeof(state->end_name),"%s",sales_text(sale,0));
                        state->error[0] = '\0';
                    }
                    sqlite3_finalize(sale);
                }
            }
        }
        ImGui_Dummy((ImVec2){0,4});
        ImGui_PopStyleVar();
        if (save && sales_save_form(shop)) {
            snprintf(state->message, sizeof(state->message), "%s %s.", state->form == 1 ? "Discount code" : "Sale",state->editing_id ? "updated" : "created");
            memset(state->pages,0,sizeof(state->pages));
            sales_close_form(shop);
        } else if (cancel) {
            sales_close_form(shop);
            state->error[0] = '\0';
        }
        if (state->form) {
            sales_delete_confirmation(shop);
            sales_end_confirmation(shop);
        }
    }
    ImGui_End();
    ImGui_PopStyleVarEx(2);
}

static void sales_header_row(void) {
    ImVec4 background = ImGui_GetStyle()->Colors[ImGuiCol_TableHeaderBg];
    ImGui_PushStyleColorImVec4(ImGuiCol_Header,background);
    ImGui_PushStyleColorImVec4(ImGuiCol_HeaderHovered,background);
    ImGui_PushStyleColorImVec4(ImGuiCol_HeaderActive,background);
    ImGui_PushFontFloat(NULL,22.0f);
    ImGui_TableHeadersRow();
    ImGui_PopFont();
    ImGui_PopStyleColorEx(3);
}

static void sales_table_headers(const char* const* columns, int count) {
    const float item_weights[] = {1.3f,1.7f,1.6f,1.2f,0.7f,1.9f,1.9f,1.0f,2.2f};
    const float code_weights[] = {1.5f,1.25f,1.0f,1.5f,2.1f,1.7f};
    for (int i = 0; i < count; i++) {
        float weight = count == 9 ? item_weights[i] : count == 6 ? code_weights[i] : i == 0 ? 2.0f : 1.0f;
        bool action = (count == 6 || count == 9) && i == count-1;
        ImGui_TableSetupColumnEx(columns[i],action ? ImGuiTableColumnFlags_WidthFixed : ImGuiTableColumnFlags_WidthStretch,
                                action ? 100.0f : weight,0);
    }
    sales_header_row();
}

static void sales_cell(const char* text) {
    ImGui_TableNextColumn();
    ImGui_TextUnformatted(text);
}

static void sales_money_cell(double value) {
    char text[128];
    format_money(text, sizeof(text), value);
    sales_cell(text);
}

static void sales_time_cell(sqlite3_int64 value) {
    char text[32];
    sales_format_time(value, text, sizeof(text));
    sales_cell(text);
}

static void sales_pagination(int* page, int count, Sales_State* state, int page_size) {
    if (count == 0) { ImGui_TextDisabled("No entries in this section."); return; }
    char range[64];
    snprintf(range,sizeof(range),"Showing %d-%d of %d",*page*page_size+1,MIN((*page+1)*page_size,count),count);
    ImGuiStyle* style = ImGui_GetStyle();
    float width = ImGui_CalcTextSize("Prev").x+ImGui_CalcTextSize(range).x+ImGui_CalcTextSize("Next").x+
                  style->FramePadding.x*4+style->ItemSpacing.x*2;
    ImGui_SetCursorPosX(ImGui_GetCursorPosX()+fmaxf(0,(ImGui_GetContentRegionAvail().x-width)*0.5f));
    ImGui_BeginDisabled(*page == 0);
    if (ImGui_Button("Prev")) { (*page)--; state->expanded_sale = 0; }
    ImGui_EndDisabled();
    ImGui_SameLine();
    ImGui_TextUnformatted(range);
    ImGui_SameLine();
    ImGui_BeginDisabled((*page+1)*page_size >= count);
    if (ImGui_Button("Next")) { (*page)++; state->expanded_sale = 0; }
    ImGui_EndDisabled();
}

static const char* sales_status_filter(int status) {
    static const char* filters[] = {
        "s.ended_at IS NULL AND s.starts_at<=unixepoch() AND (s.ends_at IS NULL OR s.ends_at>unixepoch())",
        "s.ended_at IS NULL AND s.starts_at>unixepoch()",
        "s.ended_at IS NOT NULL OR s.ends_at<=unixepoch()",
    };
    return filters[status];
}

static void sales_group_items(Shop* shop, sqlite3_int64 sale_id, bool historical) {
    sqlite3_stmt* stmt = sales_prepare(shop,
        "SELECT si.product_name,si.regular_price,si.sale_price,i.display,i.price,s.discount_value,count(*) OVER() "
        "FROM sale_items si JOIN sales s ON s.id=si.sale_id LEFT JOIN items i ON i.id=si.item_id "
        "WHERE si.sale_id=? ORDER BY si.product_name,si.item_id");
    if (!stmt) return;
    sqlite3_bind_int64(stmt, 1, sale_id);
    const char* columns[] = {"Product","Regular Price","Sale Price"};
    int rc = sqlite3_step(stmt);
    int count = rc == SQLITE_ROW ? sqlite3_column_int(stmt,6) : 0;
    float height = 22.0f+ImGui_GetStyle()->CellPadding.y*2+
                   (ImGui_GetTextLineHeight()+ImGui_GetStyle()->CellPadding.y*2)*MIN(5,count);
    if (ImGui_BeginTableEx("Included products", 3, ImGuiTableFlags_RowBg |
        ImGuiTableFlags_ScrollY | ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_NoSavedSettings, (ImVec2){0,height}, 0)) {
        ImGui_TableSetupScrollFreeze(0,1);
        sales_table_headers(columns,3);
        while (rc == SQLITE_ROW) {
            ImGui_TableNextRow();
            bool use_snapshot = historical || sqlite3_column_type(stmt,4) == SQLITE_NULL;
            double regular = sqlite3_column_double(stmt, use_snapshot ? 1 : 4);
            sales_cell(sales_text(stmt,use_snapshot ? 0 : 3));
            sales_money_cell(regular);
            sales_money_cell(use_snapshot ? sqlite3_column_double(stmt,2) : sales_price(regular,true,sqlite3_column_double(stmt,5)));
            rc = sqlite3_step(stmt);
        }
        ImGui_EndTable();
    }
    sqlite3_finalize(stmt);
}

static void sales_page_color(ImGuiCol slot, Color color) {
    ImGui_PushStyleColorImVec4(slot,(ImVec4){color.r/255.0f,color.g/255.0f,color.b/255.0f,color.a/255.0f});
}

static void sales_status_cell(const char* status) {
    Color color = GRAY;
    if (strcmp(status,"Active") == 0) color = SHOP_GREEN;
    else if (strcmp(status,"Ended") == 0 || strcmp(status,"Expired") == 0) color = SHOP_RED;
    else if (strcmp(status,"Scheduled") == 0) color = SHOP_BLUE;
    ImGui_TableNextColumn();
    sales_page_color(ImGuiCol_Text,color);
    ImGui_TextUnformatted(status);
    ImGui_PopStyleColor();
}

static void sales_tooltip(Shop* shop, const char* text) {
    ImVec2 view = ImGui_GetIO()->DisplaySize;
    float scale = fminf(view.x/shop->render_target.texture.width,view.y/shop->render_target.texture.height);
    ImVec2 size = {ImGui_CalcTextSize(text).x+20,ImGui_GetFontSize()+14};
    ImVec2 margin = {(view.x-shop->render_target.texture.width*scale)/2+24,
                     (view.y-shop->render_target.texture.height*scale)/2+24};
    ImVec2 mouse = ImGui_GetIO()->MousePos;
    ImGui_SetNextWindowPos((ImVec2){Clamp(mouse.x+16,margin.x,view.x-margin.x-size.x),
                                  Clamp(mouse.y+20,margin.y,view.y-margin.y-size.y)},ImGuiCond_Always);
    ImGui_SetNextWindowSize(size,ImGuiCond_Always);
    sales_page_color(ImGuiCol_WindowBg,SHOP_BG);
    sales_page_color(ImGuiCol_Text,SHOP_INK);
    sales_page_color(ImGuiCol_Border,SHOP_INK);
    ImGui_PushStyleVarImVec2(ImGuiStyleVar_WindowPadding,(ImVec2){10,7});
    ImGui_PushStyleVar(ImGuiStyleVar_WindowRounding,5);
    ImGui_PushStyleVar(ImGuiStyleVar_WindowBorderSize,1.5f);
    if (ImGui_Begin("##sales_page_tooltip",NULL,ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
        ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoInputs |
        ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoBringToFrontOnFocus)) ImGui_TextUnformatted(text);
    ImGui_End();
    ImGui_PopStyleVarEx(3);
    ImGui_PopStyleColorEx(3);
}

static void sales_edit_button(Shop* shop, int form, sqlite3_int64 id) {
    sales_page_color(ImGuiCol_Button,SHOP_BLUE);
    sales_page_color(ImGuiCol_ButtonHovered,ColorBrightness(SHOP_BLUE,-0.5f));
    sales_page_color(ImGuiCol_ButtonActive,ColorBrightness(SHOP_BLUE,-0.8f));
    bool edit = ImGui_ButtonEx(ICON_FA_PENCIL "##edit",(ImVec2){36,0});
    ImGui_PopStyleColorEx(3);
    if (edit) sales_open_edit(shop,form,id);
}

static void sales_row_actions(Shop* shop, sqlite3_int64 id, int status, bool paused, int form) {
    sales_edit_button(shop,form,id);
    if (status == 2) return;
    ImGui_SameLineEx(0,8);
    if (status == 0) {
        Color color = paused ? SHOP_GREEN : GRAY;
        sales_page_color(ImGuiCol_Button,color);
        sales_page_color(ImGuiCol_ButtonHovered,ColorBrightness(color,-0.5f));
        sales_page_color(ImGuiCol_ButtonActive,ColorBrightness(color,-0.8f));
        bool toggle = ImGui_ButtonEx(paused ? ICON_FA_PLAY "##resume" : ICON_FA_PAUSE "##pause",(ImVec2){36,0});
        if (ImGui_IsItemHovered(0)) sales_tooltip(shop,paused ? "Resume Sale" : "Pause Sale");
        ImGui_PopStyleColorEx(3);
        if (toggle && sales_set_paused(shop,id,!paused))
            snprintf(shop->sales.message,sizeof(shop->sales.message),"Sale %s.",paused ? "resumed" : "paused");
    }

}

static void sales_end_confirmation(Shop* shop) {
    Sales_State* state = &shop->sales;
    if (state->open_end_confirmation) {
        ImGui_OpenPopup("End Sale",0);
        state->open_end_confirmation = false;
    }
    ImVec2 view = ImGui_GetIO()->DisplaySize;
    ImGui_SetNextWindowPosEx((ImVec2){view.x/2,view.y/2},ImGuiCond_Appearing,(ImVec2){0.5f,0.5f});
    ImGui_SetNextWindowSizeConstraints((ImVec2){fminf(420,view.x-24),0},(ImVec2){fminf(520,view.x-24),view.y-24},NULL,NULL);
    if (panel_begin_modal("End Sale",ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoSavedSettings)) {
        ImGui_TextUnformatted("Are you sure you want to end");
        ImGui_SameLine();
        static ImFont* bold = NULL;
        if (!bold) bold = ImFontAtlas_AddFontFromFileTTF(ImGui_GetIO()->Fonts,"assets/JetBrainsMono-Bold.ttf",0,NULL,NULL);
        ImGui_PushFontFloat(bold,ImGui_GetStyle()->FontSizeBase);
        ImGui_TextWrapped("%s?",state->end_name);
        ImGui_PopFont();
        if (state->error[0]) ImGui_TextWrapped("%s",state->error);
        float row_width = 202.0f+ImGui_GetStyle()->ItemSpacing.x;
        ImGui_SetCursorPosX(ImGui_GetCursorPosX()+fmaxf(0,(ImGui_GetContentRegionAvail().x-row_width)/2));
        sales_page_color(ImGuiCol_Button,SHOP_RED);
        sales_page_color(ImGuiCol_ButtonHovered,ColorBrightness(SHOP_RED,-0.5f));
        sales_page_color(ImGuiCol_ButtonActive,ColorBrightness(SHOP_RED,-0.8f));
        sales_page_color(ImGuiCol_Text,BLACK);
        bool end_sale = ImGui_ButtonEx("End Sale",(ImVec2){112,32});
        ImGui_PopStyleColorEx(4);
        bool ended = end_sale && sales_end(shop,state->confirm_end);
        if (ended) {
            snprintf(state->message,sizeof(state->message),"Sale ended. Its history has been kept.");
            state->confirm_end = 0;
            ImGui_CloseCurrentPopup();
        }
        ImGui_SameLine();
        if (ImGui_ButtonEx("Cancel",(ImVec2){90,32})) {
            state->confirm_end = 0;
            state->error[0] = '\0';
            ImGui_CloseCurrentPopup();
        }
        ImGui_EndPopup();
        if (ended) {
            sales_close_form(shop);
            memset(state->pages,0,sizeof(state->pages));
        }
    }
}

#include "sales_search.c"

static void sales_section(Shop* shop, int tab, int status) {
    Sales_State* state = &shop->sales;
    const char* from;
    char* filter;
    if (tab == 0) {
        from = "discount_codes d";
        const char* active = "d.is_active=1 AND " DISCOUNT_NOT_EXPIRED;
        filter = sqlite3_mprintf(status == 0 ? "d.deleted_at IS NULL AND (%s)" : "d.deleted_at IS NULL AND NOT (%s)", active);
    } else {
        from = tab == 1 ? "sales s JOIN sale_items si ON si.sale_id=s.id LEFT JOIN items i ON i.id=si.item_id" : "sales s";
        filter = sqlite3_mprintf(tab == 1 ? "(%s)" : "s.kind='group' AND (%s)",sales_status_filter(status));
    }
    char* search = sales_search_clause(shop,tab);
    if (!search) {
        sqlite3_free(filter);
        sales_error(shop,"Not enough memory to search sales.");
        return;
    }
    filter = sqlite3_mprintf("(%z) AND (%z)",filter,search);
    char* sql = sqlite3_mprintf("SELECT count(*) FROM %s WHERE %s",from,filter);
    sqlite3_stmt* stmt = sales_prepare(shop,sql);
    sqlite3_free(sql);
    if (!stmt) { sqlite3_free(filter); return; }
    int count = sqlite3_step(stmt) == SQLITE_ROW ? sqlite3_column_int(stmt,0) : 0;
    sqlite3_finalize(stmt);
    int* page = &state->pages[tab][status];
    *page = MIN(*page,MAX(0,(count-1)/10));
    const char* label = tab == 0 ? (status == 0 ? "Active" : "Inactive") : (status == 0 ? "Ongoing" : status == 1 ? "Scheduled" : "Past");
    char heading[64];
    snprintf(heading,sizeof(heading),"%s (%d)###section_%d_%d",label,count,tab,status);
    ImGui_PushIDInt(tab*3+status);
    bool open = ImGui_CollapsingHeader(heading,status == 0 ? ImGuiTreeNodeFlags_DefaultOpen : 0);
    ImVec2 section_min = ImGui_GetItemRectMin();
    float section_width = ImGui_GetItemRectSize().x;
    if (!open) { sqlite3_free(filter); ImGui_PopID(); return; }
    const char* select = tab == 0 ?
        "d.id,d.code,d.discount_type,d.discount_value,d.is_active,COALESCE(d.expiration_date,''),datetime(d.created_at,'localtime'),"
        "CASE WHEN NOT " DISCOUNT_NOT_EXPIRED " THEN 'Expired' WHEN d.is_active=0 THEN 'Disabled' ELSE 'Active' END" :
        tab == 1 ? "s.id,COALESCE(i.display,si.product_name),si.regular_price,si.sale_price,s.kind,s.name,s.starts_at,"
        "COALESCE(s.ended_at,s.ends_at,0),i.price,s.discount_type,s.discount_value,s.is_paused,si.rowid" :
        "s.id,s.name,s.discount_value,(SELECT count(*) FROM sale_items WHERE sale_id=s.id),s.starts_at,COALESCE(s.ended_at,s.ends_at,0),s.is_paused";
    char* order = sales_sort_order(shop,tab);
    sql = sqlite3_mprintf("SELECT %s FROM %s WHERE %s ORDER BY %s LIMIT 10 OFFSET %d",select,from,filter,order,*page*10);
    sqlite3_free(order);
    sqlite3_free(filter);
    stmt = sales_prepare(shop,sql);
    sqlite3_free(sql);
    if (!stmt) { ImGui_PopID(); return; }
    const char* code_columns[] = {"Code","Discount","Status","Expires","Created","Action"};
    const char* item_columns[] = {"Group","Product","Regular Price","Sale Price","% Off","Starts","Ends","Status","Action"};
    const char* group_columns[] = {"Name","% Off","Items","Starts","Ends","Status","Action"};
    int columns = tab == 1 ? 9 : 6;
    int toggle_code = 0;
    float row_height = ImGui_GetFrameHeight()+ImGui_GetStyle()->CellPadding.y*2;
    float content_x = ImGui_GetCursorPosX();
    ImGui_SetCursorScreenPos((ImVec2){section_min.x,ImGui_GetCursorScreenPos().y});
    float available_width = section_width;
    float inner_width = fmaxf(available_width,tab == 1 ? 1500.0f : 860.0f);
    float table_height = 22.0f+ImGui_GetStyle()->CellPadding.y*2+row_height*MIN(10,count-*page*10);
    if (inner_width > available_width) table_height += ImGui_GetStyle()->ScrollbarSize;
    if (tab != 2 && count && ImGui_BeginTableEx("##sales_table",columns,ImGuiTableFlags_RowBg |
        ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_NoSavedSettings | ImGuiTableFlags_ScrollX | ImGuiTableFlags_PadOuterX,
        (ImVec2){available_width,table_height},inner_width)) {
        sales_table_headers(tab == 0 ? code_columns : tab == 1 ? item_columns : group_columns,columns);
        while (sqlite3_step(stmt) == SQLITE_ROW) {
            sqlite3_int64 id = sqlite3_column_int64(stmt,0);
            ImGui_PushIDInt((int)sqlite3_column_int64(stmt,tab == 1 ? 12 : 0));
            // Preserve row height when horizontally clipped action buttons are skipped by ImGui.
            ImGui_TableNextRowEx(0,row_height);
            if (tab == 0) {
                sales_cell(sales_text(stmt,1));
                ImGui_TableNextColumn();
                if (strcmp(sales_text(stmt,2),"percent") == 0) ImGui_Text("%.2f%% off",sqlite3_column_double(stmt,3));
                else ImGui_Text("$%.2f off",sqlite3_column_double(stmt,3));
                sales_status_cell(sales_text(stmt,7));
                sales_cell(*sales_text(stmt,5) ? sales_text(stmt,5) : "No Expiration");
                sales_cell(sales_text(stmt,6));
                ImGui_TableNextColumn();
                sales_edit_button(shop,1,id);
                ImGui_SameLineEx(0,8);
                bool expired = strcmp(sales_text(stmt,7),"Expired") == 0;
                ImGui_BeginDisabled(expired);
                bool enabled = sqlite3_column_int(stmt,4) != 0;
                Color toggle_color = enabled ? GRAY : SHOP_GREEN;
                sales_page_color(ImGuiCol_Button,toggle_color);
                sales_page_color(ImGuiCol_ButtonHovered,ColorBrightness(toggle_color,-0.5f));
                sales_page_color(ImGuiCol_ButtonActive,ColorBrightness(toggle_color,-0.8f));
                if (ImGui_ButtonEx(enabled ? ICON_FA_PAUSE "##disable" : ICON_FA_PLAY "##enable",(ImVec2){36,0})) toggle_code = (int)id;
                if (ImGui_IsItemHovered(0)) sales_tooltip(shop,enabled ? "Disable Code" : "Enable Code");
                ImGui_PopStyleColorEx(3);
                ImGui_EndDisabled();
            } else if (tab == 1) {
                bool snapshot = status == 2 || sqlite3_column_type(stmt,8) == SQLITE_NULL;
                double regular = sqlite3_column_double(stmt,snapshot ? 2 : 8);
                double price = snapshot ? sqlite3_column_double(stmt,3) : sales_price(regular,
                    strcmp(sales_text(stmt,9),"percent")==0,sqlite3_column_double(stmt,10));
                sales_cell(strcmp(sales_text(stmt,4),"group") == 0 ? sales_text(stmt,5) : "None");
                sales_cell(sales_text(stmt,1));
                sales_money_cell(regular);
                sales_money_cell(price);
                ImGui_TableNextColumn(); ImGui_Text("%.2f%%",regular > 0 ? (1-price/regular)*100 : 0);
                sales_time_cell(sqlite3_column_int64(stmt,6));
                sales_time_cell(sqlite3_column_int64(stmt,7));
                bool paused = sqlite3_column_int(stmt,11) != 0;
                sales_status_cell(status == 2 ? "Ended" : status == 1 ? "Scheduled" : paused ? "Paused" : "Active");
                ImGui_TableNextColumn();
                if (strcmp(sales_text(stmt,4),"individual") == 0) sales_row_actions(shop,id,status,paused,2);
                else sales_edit_button(shop,3,id);
            }
            ImGui_PopID();
        }
        ImGui_EndTable();
    }
    if (tab == 2 && count) {
        ImGui_SetNextWindowContentSize((ImVec2){fmaxf(1160,available_width),0});
        bool visible = ImGui_BeginChild("##group_rows",(ImVec2){available_width,0},ImGuiChildFlags_AutoResizeY,ImGuiWindowFlags_HorizontalScrollbar);
        int row = 0;
        ImGui_PushStyleVarImVec2(ImGuiStyleVar_ItemSpacing,(ImVec2){12,0});
        while (visible && sqlite3_step(stmt) == SQLITE_ROW) {
            sqlite3_int64 id = sqlite3_column_int64(stmt,0);
            ImGui_PushIDInt((int)id);
            if (ImGui_BeginTable("##group_row",7,ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingStretchProp |
                ImGuiTableFlags_NoSavedSettings | ImGuiTableFlags_PadOuterX)) {
                const float weights[] = {2.1f,0.7f,0.5f,1.7f,1.7f,1.0f,2.8f};
                for (int c = 0; c < 7; c++) ImGui_TableSetupColumnEx(group_columns[c],
                    c == 6 ? ImGuiTableColumnFlags_WidthFixed : ImGuiTableColumnFlags_WidthStretch,c == 6 ? 100.0f : weights[c],0);
                if (row == 0) {
                    sales_header_row();
                }
                ImGui_TableNextRowEx(0,row_height);
                ImGui_TableSetBgColor(ImGuiTableBgTarget_RowBg0,ImGui_GetColorU32(row%2 ? ImGuiCol_TableRowBgAlt : ImGuiCol_TableRowBg),-1);
                ImGui_TableNextColumn();
                bool expanded = state->expanded_sale == id;
                char row_label[180];
                snprintf(row_label,sizeof(row_label),"%s %s",expanded ? "-" : "+",sales_text(stmt,1));
                if (ImGui_SelectableEx(row_label,expanded,ImGuiSelectableFlags_SpanAllColumns | ImGuiSelectableFlags_AllowOverlap,(ImVec2){0,0}))
                    state->expanded_sale = expanded ? 0 : id;
                ImGui_TableNextColumn(); ImGui_Text("%.2f%%",sqlite3_column_double(stmt,2));
                ImGui_TableNextColumn(); ImGui_Text("%d",sqlite3_column_int(stmt,3));
                sales_time_cell(sqlite3_column_int64(stmt,4));
                sales_time_cell(sqlite3_column_int64(stmt,5));
                bool paused = sqlite3_column_int(stmt,6) != 0;
                sales_status_cell(status == 2 ? "Ended" : status == 1 ? "Scheduled" : paused ? "Paused" : "Active");
                ImGui_TableNextColumn();
                sales_row_actions(shop,id,status,paused,3);
                ImGui_EndTable();
            }
            if (state->expanded_sale == id) sales_group_items(shop,id,status == 2);
            row++;
            ImGui_PopID();
        }
        ImGui_PopStyleVar();
        ImGui_EndChild();
    }
    ImGui_SetCursorPosX(content_x);
    sqlite3_finalize(stmt);
    sales_pagination(page,count,state,10);
    if (toggle_code && auth_has_admin_access(shop)) {
        stmt = sales_prepare(shop,"UPDATE discount_codes SET is_active=NOT is_active WHERE id=? AND deleted_at IS NULL");
        if (stmt) {
            sqlite3_bind_int(stmt,1,toggle_code);
            if (sqlite3_step(stmt) != SQLITE_DONE) sales_error(shop,"Could not update this code.");
            sqlite3_finalize(stmt);
        }
    }
    ImGui_PopID();
}

static int sales_page_theme(void) {
    const struct { ImGuiCol slot; Color color; } colors[] = {
        {ImGuiCol_WindowBg, SHOP_BG},
        {ImGuiCol_PopupBg, SHOP_BG},
        {ImGuiCol_ChildBg, SHOP_BG_ALT},
        {ImGuiCol_Text, SHOP_INK},
        {ImGuiCol_TextDisabled, (Color){105,92,64,255}},
        {ImGuiCol_Button, SHOP_BLUE},
        {ImGuiCol_ButtonHovered, ColorBrightness(SHOP_BLUE,-0.5f)},
        {ImGuiCol_ButtonActive, ColorBrightness(SHOP_BLUE,-0.8f)},
        {ImGuiCol_Header, SHOP_BG_ALT},
        {ImGuiCol_HeaderHovered, (Color){229,219,165,255}},
        {ImGuiCol_HeaderActive, SHOP_SHADOW},
        {ImGuiCol_Tab, ColorBrightness(SHOP_BLUE,0.75f)},
        {ImGuiCol_TabHovered, ColorBrightness(SHOP_BLUE,-0.5f)},
        {ImGuiCol_TabSelected, SHOP_BLUE},
        {ImGuiCol_TabSelectedOverline, ColorBrightness(SHOP_BLUE,-0.25f)},
        {ImGuiCol_TabDimmed, ColorBrightness(SHOP_BLUE,0.75f)},
        {ImGuiCol_TabDimmedSelected, SHOP_BLUE},
        {ImGuiCol_TabDimmedSelectedOverline, ColorBrightness(SHOP_BLUE,-0.25f)},
        {ImGuiCol_TableHeaderBg, SHOP_BG_ALT},
        {ImGuiCol_TableBorderStrong, SHOP_SHADOW},
        {ImGuiCol_TableBorderLight, SHOP_SHADOW},
        {ImGuiCol_TableRowBg, SHOP_BG},
        {ImGuiCol_TableRowBgAlt, SHOP_WHITE},
        {ImGuiCol_Separator, SHOP_SHADOW},
        {ImGuiCol_Border, SHOP_SHADOW},
        {ImGuiCol_ScrollbarBg, SHOP_SHADOW},
        {ImGuiCol_ScrollbarGrab, SHOP_TEXT_MUTED},
        {ImGuiCol_ScrollbarGrabHovered, ColorBrightness(SHOP_TEXT_MUTED,0.15f)},
        {ImGuiCol_ScrollbarGrabActive, ColorBrightness(SHOP_TEXT_MUTED,-0.15f)},
    };
    int count = (int)(sizeof(colors)/sizeof(colors[0]));
    for (int i = 0; i < count; i++) sales_page_color(colors[i].slot, colors[i].color);
    return count;
}

static Rectangle sales_viewport(Shop* shop) {
    ImVec2 view = ImGui_GetIO()->DisplaySize;
    float scale = fminf(view.x/shop->render_target.texture.width,view.y/shop->render_target.texture.height);
    float width = shop->render_target.texture.width*scale;
    float height = shop->render_target.texture.height*scale;
    return (Rectangle){(view.x-width)*0.5f,(view.y-height)*0.5f,width,height};
}

static Rectangle sales_create_button_bounds(Shop* shop) {
    Rectangle bounds = back_button_bounds(shop);
    bounds.width = 260.0f;
    bounds.x = shop->render_target.texture.width-24.0f-bounds.width;
    return bounds;
}

static void dashboard_back_to_top(Shop* shop, bool show, bool blocked, float* alpha, bool* scroll_to_top) {
    Rectangle viewport = sales_viewport(shop);
    float page_scale = viewport.width/shop->render_target.texture.width;
    *alpha = show
        ? fminf(1.0f,*alpha+ImGui_GetIO()->DeltaTime/0.2f)
        : fmaxf(0.0f,*alpha-ImGui_GetIO()->DeltaTime/0.2f);
    if (*alpha > 0.0f) {
        float size = 60.0f*page_scale;
        float top = shop->screen == USERS_SCREEN
            ? back_button_bounds(shop).y+(back_button_bounds(shop).height-60.0f)*0.5f
            : shop->render_target.texture.height-180.0f;
        ImGui_SetNextWindowPos((ImVec2){viewport.x+viewport.width-24.0f*page_scale-size,
                                      viewport.y+top*page_scale},ImGuiCond_Always);
        ImGui_SetNextWindowSize((ImVec2){size,size},ImGuiCond_Always);
        ImGui_SetNextWindowScroll((ImVec2){0,0});
        ImGui_PushStyleVarImVec2(ImGuiStyleVar_WindowPadding,(ImVec2){0,0});
        ImGui_PushStyleVar(ImGuiStyleVar_Alpha,ImGui_GetStyle()->Alpha * (*alpha));
        if (ImGui_Begin("##sales_page_top",NULL,ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
            ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse |
            ImGuiWindowFlags_NoBackground | ImGuiWindowFlags_NoFocusOnAppearing)) {
            ImGui_BeginDisabled(blocked);
            if (ImGui_ButtonEx(ICON_FA_CHEVRON_UP "##back_to_top",(ImVec2){size,size})) *scroll_to_top = true;
            if (ImGui_IsItemHovered(0)) sales_tooltip(shop,"Back to Top");
            ImGui_EndDisabled();
        }
        ImGui_End();
        ImGui_PopStyleVarEx(2);
    }
}

static void sales_dashboard(Shop* shop) {
    if (!auth_has_admin_access(shop)) {
        sales_close_form(shop);
        shop->screen = HOME_SCREEN;
        return;
    }
    Sales_State* state = &shop->sales;
    Rectangle viewport = sales_viewport(shop);
    float page_scale = viewport.width/shop->render_target.texture.width;
    ImGui_SetNextWindowPos((ImVec2){viewport.x,viewport.y},ImGuiCond_Always);
    ImGui_SetNextWindowSize((ImVec2){viewport.width,viewport.height-130.0f*page_scale},ImGuiCond_Always);
    int page_colors = sales_page_theme();
    ImGui_PushFontFloat(NULL,18.0f);
    ImGui_PushStyleVarImVec2(ImGuiStyleVar_WindowPadding,(ImVec2){50,40});
    ImGui_PushStyleVarImVec2(ImGuiStyleVar_FramePadding,(ImVec2){10,7});
    ImGui_PushStyleVarImVec2(ImGuiStyleVar_ItemSpacing,(ImVec2){12,12});
    ImGui_PushStyleVarImVec2(ImGuiStyleVar_CellPadding,(ImVec2){10,8});
    ImGui_PushStyleVar(ImGuiStyleVar_WindowBorderSize,0);
    ImGui_PushStyleVar(ImGuiStyleVar_FrameRounding,5);
    ImGui_PushStyleVar(ImGuiStyleVar_ScrollbarSize,12);
    ImGui_PushStyleVar(ImGuiStyleVar_ScrollbarRounding,0);
    ImGui_PushStyleVar(ImGuiStyleVar_GrabMinSize,24);
    ImGui_SetNextWindowContentSize((ImVec2){fmaxf(1,viewport.width-100-ImGui_GetStyle()->ScrollbarSize),0});
    ImGui_SetNextWindowScroll((ImVec2){0,state->scroll_to_top ? 0 : -1});
    state->scroll_to_top = false;
    bool hide_scrollbar = !state->page_can_scroll;
    if (hide_scrollbar) {
        ImGui_PushStyleColor(ImGuiCol_ScrollbarBg,0);
        ImGui_PushStyleColor(ImGuiCol_ScrollbarGrab,0);
        ImGui_PushStyleColor(ImGuiCol_ScrollbarGrabHovered,0);
        ImGui_PushStyleColor(ImGuiCol_ScrollbarGrabActive,0);
    }
    bool page_visible = ImGui_Begin("##sales_page",NULL,ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoMove |
        ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_AlwaysVerticalScrollbar);
    if (hide_scrollbar) ImGui_PopStyleColorEx(4);
    if (page_visible) {
        state->page_can_scroll = ImGui_GetScrollMaxY() > 0;
        state->scroll_y = ImGui_GetScrollY();
        float threshold = fminf(ImGui_GetWindowHeight()*0.8f,ImGui_GetScrollMaxY()*0.8f);
        state->show_back_to_top = threshold > 0 && state->scroll_y >= threshold;
        ImGui_BeginDisabled(state->form != 0 || state->confirm_end != 0 || state->advanced_open || shop->transitioning);
        float header_bottom = 90.0f*page_scale;
        ImGui_Dummy((ImVec2){0,fmaxf(40.0f,header_bottom-ImGui_GetCursorPosY())});
        ImGui_Spacing();
        if (!state->form && state->message[0]) ImGui_TextWrapped("%s",state->message);
        if (!state->form && state->error[0]) ImGui_TextWrapped("%s",state->error);
        const char* tabs[] = {"Discount Codes","Items on Sale","Group Sales"};
        const int tab_order[] = {0,2,1};
        if (ImGui_BeginTabBar("##sales_tabs",0)) {
            for (int i = 0; i < 3; i++) {
                int tab = tab_order[i];
                if (!ImGui_BeginTabItem(tabs[tab],NULL,0)) continue;
                state->current_tab = tab;
                for (int section = 0; section < (tab == 0 ? 2 : 3); section++) sales_section(shop,tab,section);
                ImGui_EndTabItem();
            }
            ImGui_EndTabBar();
        }
        ImGui_EndDisabled();
    }
    ImGui_End();
    dashboard_back_to_top(shop,state->show_back_to_top,
        state->form || state->confirm_end || state->advanced_open || shop->transitioning,
        &state->back_to_top_alpha,&state->scroll_to_top);
    ImGui_PopStyleVarEx(9);
    ImGui_PopFont();
    ImGui_PopStyleColorEx(page_colors);
    if (state->form) sales_form_panel(shop);
    if (state->advanced_open) sales_advanced_search(shop);
}

static bool sales_page_draw_list(const ImDrawList* list) {
    const char* owner = list->_OwnerName;
    return owner && (strncmp(owner,"##sales_page",12) == 0 || strncmp(owner,"##users_page",12) == 0);
}

static void sales_render_ui(Shop* shop) {
    bool users = shop->screen == USERS_SCREEN;
    RenderTexture2D* target = users ? &shop->users.page_target : &shop->sales.page_target;
    Rectangle viewport = sales_viewport(shop);
    int width = (int)roundf(viewport.width*GetRenderWidth()/GetScreenWidth());
    int height = (int)roundf(viewport.height*GetRenderHeight()/GetScreenHeight());
    if (target->texture.width != width || target->texture.height != height) {
        RenderTexture2D resized = LoadRenderTexture(width,height);
        if (!IsRenderTextureValid(resized)) {
            sales_error(shop,"Could not prepare the Sales display. Try resizing the window.");
            rlImGuiEnd();
            return;
        }
        if (target->id) UnloadRenderTexture(*target);
        *target = resized;
    }

    ImGui_Render();
    ImDrawData* data = ImGui_GetDrawData();
    ImDrawData original = *data;
    int max_commands = 0;
    for (int i = 0; i < original.CmdLists.Size; i++)
        max_commands = MAX(max_commands,original.CmdLists.Data[i]->CmdBuffer.Size);
    ImDrawCmd* commands = malloc(MAX(1,max_commands)*sizeof(*commands));
    if (!commands) {
        rlImGuiEnd();
        return;
    }
    ImGuiIO* io = ImGui_GetIO();
    ImVec2 display_size = io->DisplaySize;
    io->DisplaySize = (ImVec2){viewport.width,viewport.height};
    BeginTextureMode(*target);
    ClearBackground(SHOP_BG);
    // ImGui vertices use window coordinates; its scissor rectangles use framebuffer pixels.
    rlMatrixMode(RL_PROJECTION);
    rlLoadIdentity();
    rlOrtho(viewport.x,viewport.x+viewport.width,viewport.y+viewport.height,viewport.y,0,1);
    rlMatrixMode(RL_MODELVIEW);
    rlLoadIdentity();
    for (int pass = 0; pass < 2; pass++) {
        for (int i = 0; i < original.CmdLists.Size; i++) {
            ImDrawList* list = original.CmdLists.Data[i];
            if (!sales_page_draw_list(list)) continue;
            bool overlay = strcmp(list->_OwnerName,"##sales_page_tooltip") == 0 ||
                           strcmp(list->_OwnerName,"##sales_page_top") == 0;
            if (overlay != (pass == 1)) continue;
            ImDrawList local = *list;
            memcpy(commands,list->CmdBuffer.Data,list->CmdBuffer.Size*sizeof(*commands));
            for (int c = 0; c < list->CmdBuffer.Size; c++) {
                commands[c].ClipRect.x -= viewport.x;
                commands[c].ClipRect.z -= viewport.x;
                commands[c].ClipRect.y -= viewport.y;
                commands[c].ClipRect.w -= viewport.y;
            }
            local.CmdBuffer.Data = commands;
            ImDrawList* local_list = &local;
            data->CmdLists.Data = &local_list;
            data->CmdLists.Size = data->CmdListsCount = 1;
            // Render() is idempotent after ImGui_Render(), so this only submits this draw list.
            rlImGuiEnd();
        }
        if (pass == 0) {
            float scale = fminf((float)GetScreenWidth()/shop->render_target.texture.width,
                                (float)GetScreenHeight()/shop->render_target.texture.height);
            rlPushMatrix();
            rlTranslatef((GetScreenWidth()-shop->render_target.texture.width*scale)*0.5f,
                         (GetScreenHeight()-shop->render_target.texture.height*scale)*0.5f,0);
            rlScalef(scale,scale,1);
            // Copy the existing shop search control at its native resolution before scaling it with the page.
            float header_scroll = users ? 0 : shop->sales.scroll_y/scale;
            if (header_scroll < 100.0f) {
                DrawTexturePro(shop->render_target.texture,
                    (Rectangle){0,shop->render_target.texture.height-100,shop->render_target.texture.width,-100},
                    (Rectangle){0,-header_scroll,shop->render_target.texture.width,100},(Vector2){0},0,WHITE);
                DrawText(users ? "USERS & ORDERS" : "SALES",50,50-header_scroll,40,BLACK);
            }
            draw_button(shop,back_button_bounds(shop),"< HOME",SHOP_GREEN);
            const char* actions[] = {"Create Code +","Put Item on Sale +","Create Group Sale +"};
            if (!users) draw_button(shop,sales_create_button_bounds(shop),actions[shop->sales.current_tab],SHOP_BLUE);
            rlPopMatrix();
        }
    }
    EndTextureMode();
    io->DisplaySize = display_size;
    free(commands);

    BeginShaderMode(shop->shader);
    DrawTexturePro(target->texture,(Rectangle){0,0,width,-height},viewport,(Vector2){0},0,WHITE);
    EndShaderMode();

    // Popup windows are submitted after the processed page, using the original UI renderer.
    for (int i = 0; i < original.CmdLists.Size; i++) {
        ImDrawList* list = original.CmdLists.Data[i];
        if (sales_page_draw_list(list)) continue;
        data->CmdLists.Data = &list;
        data->CmdLists.Size = data->CmdListsCount = 1;
        rlImGuiEnd();
    }
    *data = original;
}
