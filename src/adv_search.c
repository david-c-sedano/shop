
void do_advanced_search(Shop* shop, Adv_Search_Panel *adv_search) {
    SQL_Template sql = NEW_SQL;
    APPEND_SQL(sql, 
        "SELECT " ITEM_COLUMNS " FROM items WHERE 1=1 "
    );

    if (adv_search->keywords[0]) {
        char *escaped = sqlite3_mprintf("%q", adv_search->keywords);
        APPEND_SQL(sql,
            "AND ("
                "name LIKE '%%%s%%' "
                "OR display LIKE '%%%s%%' "
                "OR description LIKE '%%%s%%'"
            ") ",
            escaped, escaped, escaped
        );
        sqlite3_free(escaped);
    }
    if (adv_search->category[0]) {
        char *escaped = sqlite3_mprintf("%q", adv_search->category);
        APPEND_SQL(sql,
            "AND category LIKE '%%%s%%' ",
            escaped
        );
        sqlite3_free(escaped);
    }
    if (adv_search->use_min) {
        APPEND_SQL(sql, "AND price >= %f ", adv_search->min_price);
    }
    if (adv_search->use_max) {
        APPEND_SQL(sql, "AND price <= %f ", adv_search->max_price);
    }
    if (adv_search->use_stock) {
        APPEND_SQL(sql, "AND stock >= %d ", adv_search->min_stock);
    }
    if (adv_search->in_stock_only) {
        APPEND_SQL(sql, "AND stock > 0 ");
    }
    APPEND_SQL(sql,
        "ORDER BY price %s;",
        adv_search->descending ? "DESC" : "ASC"
    );
    APPEND_SQL(sql, ";");

    reset_item_list(&shop->display);
    shop->display = query_items(shop, sql.str);
    NUKE_SQL(sql);
    shop->scroll = 0.0;
    shop->scroll_target = 0.0;
}

void adv_search_panel(Shop* shop, Adv_Search_Panel *adv_search) {
    ImGui_SetNextWindowSizeConstraints(
        (ImVec2){400.0,500.0}, 
        (ImVec2){FLT_MAX, FLT_MAX}, 
        NULL,NULL
    );
    bool visible = ImGui_Begin("Advanced Search", &adv_search->active, ImGuiWindowFlags_NoCollapse);
    if (visible) {
        ImGui_Text("Search");
        ImGui_Separator();
        ImGui_TextDisabled("Keywords");
        ImGui_SetNextItemWidth(-FLT_MIN);
        ImGui_InputText("##keywords", adv_search->keywords, sizeof(adv_search->keywords), 0);

        ImGui_TextDisabled("Category");
        ImGui_SetNextItemWidth(-FLT_MIN);
        ImGui_InputText("##category", adv_search->category, sizeof(adv_search->category), 0);

        ImGui_Spacing();
        ImGui_Text("Sort");
        ImGui_Separator();
        bool price_ascending = !adv_search->descending;
        if (ImGui_Checkbox("Price Ascending", &price_ascending) && price_ascending) {
            adv_search->descending = false;
        }
        bool price_descending = adv_search->descending;
        if (ImGui_Checkbox("Price Descending", &price_descending) && price_descending) {
            adv_search->descending = true;
        }

        ImGui_Spacing();
        ImGui_Text("Filters");
        ImGui_Separator();
        ImGui_Checkbox("Minimum Price", &adv_search->use_min);
        ImGui_SameLine();
        ImGui_BeginDisabled(!adv_search->use_min);
        ImGui_SetNextItemWidth(120.0f);
        ImGui_InputFloat("##min_price", &adv_search->min_price);
        ImGui_EndDisabled();

        ImGui_Checkbox("Maximum Price", &adv_search->use_max);
        ImGui_SameLine();
        ImGui_BeginDisabled(!adv_search->use_max);
        ImGui_SetNextItemWidth(120.0f);
        ImGui_InputFloat("##max_price", &adv_search->max_price);
        ImGui_EndDisabled();

        ImGui_Checkbox("Minimum Stock", &adv_search->use_stock);
        ImGui_SameLine();
        ImGui_BeginDisabled(!adv_search->use_stock);
        ImGui_SetNextItemWidth(120.0f);
        ImGui_InputInt("##min_stock", &adv_search->min_stock);
        ImGui_EndDisabled();

        ImGui_Checkbox("In Stock Only", &adv_search->in_stock_only);

        if (adv_search->min_price < 0.0f) adv_search->min_price = 0.0f;
        if (adv_search->max_price < 0.0f) adv_search->max_price = 0.0f;
        if (adv_search->min_stock < 0) adv_search->min_stock = 0;

        ImGui_Separator();

        if (ImGui_Button("Search")) {
            if (shop->screen != DISPLAY_SCREEN) {
                screen_swap(shop, DISPLAY_SCREEN);
            } else {
                shop->search_flash = 1.0;
            }
            do_advanced_search(shop, adv_search);
            adv_search->active = false;
        }
        ImGui_SameLine();

        if (ImGui_Button("Clear")) {
            adv_search->keywords[0] = '\0';
            adv_search->category[0] = '\0';
            adv_search->min_price = 0.0;
            adv_search->max_price = 0.0;
            adv_search->min_stock = 0;
            adv_search->use_min = false;
            adv_search->use_max = false;
            adv_search->use_stock = false;
            adv_search->in_stock_only = false;
            adv_search->descending = false;
        }
        ImGui_SameLine();

        if (ImGui_Button("Cancel")) {
            adv_search->active = false;
        }
    }
    ImGui_End();
}
