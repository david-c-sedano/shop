#include "shop_db.h"
#include "shop.h" // Replace with the actual header defining Shop, Item, etc.
#include <stdio.h>
#include <stdlib.h>
#include <sqlite3.h>
#include <raylib.h>

bool shop_db_load_products(Shop* shop) {
    if (!shop || !shop->admin.db) {
        printf("[DEBUG DB] Error: Shop or database pointer is NULL!\n");
        return false;
    }

    // 1. Query total row count
    sqlite3_stmt* count_stmt = NULL;
    const char* count_sql = "SELECT COUNT(*) FROM products;";
    
    if (sqlite3_prepare_v2(shop->admin.db, count_sql, -1, &count_stmt, NULL) != SQLITE_OK) {
        printf("[DEBUG DB] Failed to prepare COUNT: %s\n", sqlite3_errmsg(shop->admin.db));
        return false;
    }

    int total_rows = 0;
    if (sqlite3_step(count_stmt) == SQLITE_ROW) {
        total_rows = sqlite3_column_int(count_stmt, 0);
    }
    sqlite3_finalize(count_stmt);

    printf("[DEBUG DB] Found %d products in database.\n", total_rows);
    if (total_rows <= 0) {
        shop->items = NULL;
        shop->item_cap = 0;
        shop->item_count = 0;
        return true;
    }

    // 2. Allocate the exact capacity
    shop->items = (Item*)calloc((size_t)total_rows, sizeof(Item));
    if (!shop->items) {
        printf("[DEBUG DB] Memory allocation failed for %d items.\n", total_rows);
        return false;
    }
    shop->item_cap = total_rows;
    shop->item_count = 0;

    // 3. Fetch and populate products
    const char* select_sql = "SELECT id, name, description, price, image_path FROM products ORDER BY id ASC;";
    sqlite3_stmt* stmt = NULL;

    if (sqlite3_prepare_v2(shop->admin.db, select_sql, -1, &stmt, NULL) != SQLITE_OK) {
        printf("[DEBUG DB] Failed to prepare SELECT: %s\n", sqlite3_errmsg(shop->admin.db));
        free(shop->items);
        shop->items = NULL;
        shop->item_cap = 0;
        return false;
    }

    while (sqlite3_step(stmt) == SQLITE_ROW && shop->item_count < shop->item_cap) {
        Item* it = &shop->items[shop->item_count];

        it->id = sqlite3_column_int(stmt, 0);

        const char* name = (const char*)sqlite3_column_text(stmt, 1);
        if (name) snprintf(it->name, sizeof(it->name), "%s", name);

        const char* desc = (const char*)sqlite3_column_text(stmt, 2);
        if (desc) snprintf(it->description, sizeof(it->description), "%s", desc);

        it->price = (float)sqlite3_column_double(stmt, 3);

        const char* path = (const char*)sqlite3_column_text(stmt, 4);
        bool file_found = path && FileExists(path);
        if (file_found) {
            it->texture = LoadTexture(path);
            SetTextureFilter(it->texture, TEXTURE_FILTER_BILINEAR);
            it->has_texture = true;
        } else {
            it->has_texture = false;
        }

        it->size = (Vector3){ 1.5f, 2.0f, 1.0f };
        it->scale = 1.0f;

        printf("  [Item %d] id=%d | name='%s' | price=$%.2f | img='%s' (exists=%s)\n",
               shop->item_count, it->id, it->name, it->price, 
               path ? path : "NULL", file_found ? "YES" : "NO");

        shop->item_count++;
    }

    sqlite3_finalize(stmt);
    return true;
}