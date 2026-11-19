#define USERS_PAGE_SIZE 8

static bool users_error(Shop* shop, const char* message) {
    snprintf(shop->users.error,sizeof(shop->users.error),"%s",message);
    return false;
}

static sqlite3_stmt* users_prepare(Shop* shop, const char* sql) {
    sqlite3_stmt* stmt = NULL;
    if (!sql || sqlite3_prepare_v2(shop->admin.db,sql,-1,&stmt,NULL) != SQLITE_OK)
        users_error(shop,"Could not load account or order data. Try again.");
    return stmt;
}

void users_collapse_all(Shop* shop) {
    free(shop->users.expanded);
    shop->users.expanded = NULL;
    shop->users.expanded_count = shop->users.expanded_capacity = 0;
    shop->users.revealed_id = 0;
}

static bool users_is_expanded(const Users_State* state, sqlite3_int64 id, sqlite3_int64 user_id) {
    for (int i=0;i<state->expanded_count;i++)
        if (state->expanded[i].id == id && state->expanded[i].user_id == user_id) return true;
    return false;
}

static void users_toggle_expanded(Shop* shop, sqlite3_int64 id, sqlite3_int64 user_id) {
    Users_State* state = &shop->users;
    state->revealed_id = 0;
    for (int i=0;i<state->expanded_count;i++) {
        Users_Expanded row = state->expanded[i];
        if (row.id == id && row.user_id == user_id) {
            state->expanded[i] = state->expanded[--state->expanded_count];
            return;
        }
    }
    if (state->expanded_count == state->expanded_capacity) {
        int capacity = state->expanded_capacity ? state->expanded_capacity*2 : 8;
        Users_Expanded* rows = realloc(state->expanded,(size_t)capacity*sizeof(*rows));
        if (!rows) { users_error(shop,"Not enough memory to expand this row."); return; }
        state->expanded = rows;state->expanded_capacity = capacity;
    }
    state->expanded[state->expanded_count++] = (Users_Expanded){id,user_id};
}

static void users_id_cell(sqlite3_int64 id) {
    char text[32];
    snprintf(text,sizeof(text),"%lld",(long long)id);
    sales_cell(text);
}

static void users_date_cell(sqlite3_stmt* stmt, int column) {
    sales_time_cell(sqlite3_column_int64(stmt,column));
}

static void users_open_edit(Shop* shop, sqlite3_int64 id) {
    if (!auth_has_admin_access(shop)) return;
    sqlite3_stmt* stmt = users_prepare(shop,"SELECT username,COALESCE(email,''),is_admin,is_enabled FROM users WHERE id=?");
    if (!stmt) return;
    sqlite3_bind_int64(stmt,1,id);
    if (sqlite3_step(stmt) == SQLITE_ROW) {
        Users_State* state = &shop->users;
        state->editing_id = id;
        state->confirmation = 0;
        state->revealed_id = 0;
        state->error[0] = '\0';
        snprintf(state->username,sizeof(state->username),"%s",sales_text(stmt,0));
        snprintf(state->original_username,sizeof(state->original_username),"%s",sales_text(stmt,0));
        snprintf(state->email,sizeof(state->email),"%s",sales_text(stmt,1));
        snprintf(state->original_email,sizeof(state->original_email),"%s",sales_text(stmt,1));
        state->edit_role = state->original_role = sqlite3_column_int(stmt,2) == 1;
        state->original_enabled = sqlite3_column_int(stmt,3) == 1;
    } else users_error(shop,"This account is no longer available.");
    sqlite3_finalize(stmt);
}

static bool users_save_account(Shop* shop, bool toggle, bool confirmed) {
    Users_State* state = &shop->users;
    if (!auth_has_admin_access(shop)) return users_error(shop,"An enabled admin account is required.");
    if (!toggle && (!username_is_valid(state->username) ||
        (state->email[0] && !email_is_valid(state->email))))
        return users_error(shop,"Use a 3–30 character username with letters, numbers, underscores or hyphens, and a valid email or leave it empty.");
    bool role_changed = !toggle && state->edit_role != state->original_role;
    if ((toggle || role_changed) && !confirmed) return users_error(shop,"Confirm this account change first.");
    if (state->editing_id == shop->account.user_id &&
        ((toggle && state->original_enabled) || (role_changed && !state->edit_role)))
        return users_error(shop,"You cannot disable or demote your signed-in account.");
    sqlite3* db = shop->admin.db;
    if (sqlite3_exec(db,"BEGIN IMMEDIATE",NULL,NULL,NULL) != SQLITE_OK)
        return users_error(shop,"The database is busy. Try saving again.");
    bool ok = auth_has_admin_access(shop);
    sqlite3_stmt* stmt = users_prepare(shop,"SELECT username,is_admin,is_enabled,COALESCE(email,'') FROM users WHERE id=?");
    if (stmt) sqlite3_bind_int64(stmt,1,state->editing_id);
    if (!stmt || sqlite3_step(stmt) != SQLITE_ROW ||
        strcmp(sales_text(stmt,0),state->original_username) != 0 ||
        strcmp(sales_text(stmt,3),state->original_email) != 0 ||
        (sqlite3_column_int(stmt,1) == 1) != state->original_role ||
        (sqlite3_column_int(stmt,2) == 1) != state->original_enabled) {
        ok = false;
        users_error(shop,"This account changed. Close the editor and reopen it before saving.");
    }
    sqlite3_finalize(stmt);
    bool removes_admin = state->original_role && state->original_enabled &&
        (toggle || (role_changed && !state->edit_role));
    if (ok && removes_admin) {
        stmt = users_prepare(shop,"SELECT count(*) FROM users WHERE is_admin=1 AND is_enabled=1 AND id!=?");
        if (stmt) sqlite3_bind_int64(stmt,1,state->editing_id);
        ok = stmt && sqlite3_step(stmt) == SQLITE_ROW && sqlite3_column_int(stmt,0) > 0;
        sqlite3_finalize(stmt);
        if (!ok) users_error(shop,"At least one enabled admin must remain.");
    }
    if (ok) {
        stmt = users_prepare(shop,toggle ? "UPDATE users SET is_enabled=? WHERE id=?" :
            "UPDATE users SET username=?,email=NULLIF(?,''),is_admin=? WHERE id=?");
        if (stmt) {
            if (toggle) {
                sqlite3_bind_int(stmt,1,!state->original_enabled);
                sqlite3_bind_int64(stmt,2,state->editing_id);
            } else {
                sqlite3_bind_text(stmt,1,state->username,-1,SQLITE_TRANSIENT);
                sqlite3_bind_text(stmt,2,state->email,-1,SQLITE_TRANSIENT);
                sqlite3_bind_int(stmt,3,state->edit_role);
                sqlite3_bind_int64(stmt,4,state->editing_id);
            }
        }
        ok = stmt && sqlite3_step(stmt) == SQLITE_DONE && sqlite3_changes(db) == 1;
        if (!ok) users_error(shop,sqlite3_errcode(db) == SQLITE_CONSTRAINT ?
            "That username or email is already in use." : "Could not update this account. Try again.");
        sqlite3_finalize(stmt);
    }
    if (ok) ok = sqlite3_exec(db,"COMMIT",NULL,NULL,NULL) == SQLITE_OK;
    if (!ok) { sqlite3_exec(db,"ROLLBACK",NULL,NULL,NULL); return false; }
    if (toggle) state->original_enabled = !state->original_enabled;
    else {
        state->original_role = state->edit_role;
        snprintf(state->original_username,sizeof(state->original_username),"%s",state->username);
        if (state->editing_id == shop->account.user_id) {
            if (strcmp(shop->account.email,state->original_email) == 0)
                snprintf(shop->account.email,sizeof(shop->account.email),"%s",state->email);
            snprintf(shop->account.current_username,sizeof(shop->account.current_username),"%s",state->username);
            snprintf(shop->account.current_email,sizeof(shop->account.current_email),"%s",state->email);
        }
    }
    if (!toggle) {
        snprintf(state->original_email,sizeof(state->original_email),"%s",state->email);
        if (state->editing_id == state->orders_user_id)
            snprintf(state->orders_username,sizeof(state->orders_username),"%s",state->username);
    }
    state->revealed_id = 0;
    state->error[0] = '\0';
    snprintf(state->message,sizeof(state->message),toggle ? "Account %s." : "Account saved.",state->original_enabled ? "enabled" : "disabled");
    return true;
}

static const char* users_from(int tab) {
    return tab == 0 ?
        "users u LEFT JOIN (SELECT user_id,count(*) AS order_count,sum(total_amount) AS spent FROM orders GROUP BY user_id) a ON a.user_id=u.id" :
        "orders o JOIN users u ON u.id=o.user_id LEFT JOIN (SELECT order_id,sum(quantity) AS quantity FROM order_items GROUP BY order_id) a ON a.order_id=o.id";
}

static sqlite3_stmt* users_query(Shop* shop, int tab, bool count, int page, sqlite3_int64 user_id) {
    Users_State* state = &shop->users;
    int page_size = state->page_sizes[user_id ? 2 : tab];
    if (!page_size) page_size = USERS_PAGE_SIZE;
    Users_Filter* filter = &state->filters[user_id ? 2 : tab];
    SQL_Template sql = NEW_SQL;
    APPEND_SQL(sql,"SELECT %s FROM %s WHERE 1=1",count ? "count(*)" : tab == 0 ?
        "u.id,u.username,COALESCE(u.email,''),u.password_hash,u.is_admin,u.is_enabled,COALESCE(a.order_count,0),COALESCE(a.spent,0),unixepoch(u.created_at)" :
        "o.id,u.username,unixepoch(o.created_at),COALESCE(a.quantity,0),o.total_amount,u.id",users_from(tab));
    if (user_id) APPEND_SQL(sql," AND u.id=%lld",(long long)user_id);
    if (tab) APPEND_SQL(sql," AND unixepoch(o.created_at)%sunixepoch()-300",
        state->orders_completed[user_id ? 1 : 0] ? "<=" : ">");
    {
        char* keyword = sqlite3_mprintf("%%%q%%",user_id ? state->orders_keywords : state->keywords);
        if (tab == 0) APPEND_SQL(sql," AND (CAST(u.id AS TEXT) LIKE '%s' OR u.username LIKE '%s' OR COALESCE(u.email,'') LIKE '%s')",keyword,keyword,keyword);
        else APPEND_SQL(sql," AND (CAST(o.id AS TEXT) LIKE '%s' OR u.username LIKE '%s' OR EXISTS(SELECT 1 FROM order_items oi WHERE oi.order_id=o.id AND oi.product_name LIKE '%s'))",keyword,keyword,keyword);
        sqlite3_free(keyword);
        if (tab == 0) {
            if (filter->role) APPEND_SQL(sql," AND u.is_admin=%d",filter->role == 2);
            if (filter->status) APPEND_SQL(sql," AND u.is_enabled=%d",filter->status == 1);
        } else {
            char* customer = sqlite3_mprintf("%%%q%%",filter->customer);
            APPEND_SQL(sql," AND u.username LIKE '%s'",customer);
            sqlite3_free(customer);
            if (filter->use_min) APPEND_SQL(sql," AND o.total_amount>=%.17g",filter->minimum);
            if (filter->use_max) APPEND_SQL(sql," AND o.total_amount<=%.17g",filter->maximum);
        }
        const char* date = tab == 0 ? "date(u.created_at,'localtime')" : "date(o.created_at,'localtime')";
        char* from = sqlite3_mprintf("%q",filter->date_from);
        char* to = sqlite3_mprintf("%q",filter->date_to);
        if (*from) APPEND_SQL(sql," AND %s>='%s'",date,from);
        if (*to) APPEND_SQL(sql," AND %s<='%s'",date,to);
        sqlite3_free(from);sqlite3_free(to);
    }
    if (!count) {
        const char* user_sorts[] = {"u.username COLLATE NOCASE","u.id","u.username COLLATE NOCASE","u.email COLLATE NOCASE","u.is_admin","u.is_enabled","COALESCE(a.order_count,0)","COALESCE(a.spent,0)","u.created_at"};
        const char* order_sorts[] = {"o.created_at","o.id","u.username COLLATE NOCASE","o.created_at","COALESCE(a.quantity,0)","o.total_amount"};
        int sort = filter->sort;
        if (sort < 0 || sort >= (tab == 0 ? 9 : 6)) sort = 0;
        bool descending = sort == 0 ? tab == 1 : filter->descending;
        APPEND_SQL(sql," ORDER BY %s %s,%s %s LIMIT %d OFFSET %d",tab == 0 ? user_sorts[sort] : order_sorts[sort],descending ? "DESC" : "ASC",tab == 0 ? "u.id" : "o.id",descending ? "DESC" : "ASC",page_size,MAX(0,page)*page_size);
    }
    sqlite3_stmt* stmt = users_prepare(shop,sql.str);
    NUKE_SQL(sql);
    return stmt;
}

static bool users_filter_valid(Shop* shop, Users_Filter* filter) {
    if ((filter->use_min && (!isfinite(filter->minimum) || filter->minimum < 0)) ||
        (filter->use_max && (!isfinite(filter->maximum) || filter->maximum < 0)) ||
        (filter->use_min && filter->use_max && filter->minimum > filter->maximum))
        return users_error(shop,"Enter a valid total range with minimum no greater than maximum.");
    const char* dates[] = {filter->date_from,filter->date_to};
    for (int i=0;i<2;i++) if (*dates[i]) {
        sqlite3_int64 parsed;
        if (!sales_parse_local(dates[i],true,&parsed)) return users_error(shop,"Choose a valid date.");
    }
    if (*dates[0] && *dates[1] && strcmp(dates[0],dates[1]) > 0)
        return users_error(shop,"The first date must be on or before the last date.");
    return true;
}

static void users_search_bar(Shop* shop, bool update) {
    Users_State* state = &shop->users;
    char* input = state->current_tab == 2 ? state->orders_search_input : state->search_input;
    char* keywords = state->current_tab == 2 ? state->orders_keywords : state->keywords;
    char previous[sizeof(shop->search_bar)];
    memcpy(previous,shop->search_bar,sizeof(previous));
    bool active = shop->search_bar_active;
    memcpy(shop->search_bar,input,sizeof(shop->search_bar));
    shop->search_bar_active = state->search_active;
    if (update) {
        shop->sales.search_offset = 0;
        if (search_bar_event(shop)) {
            snprintf(keywords,sizeof(state->keywords),"%s",shop->search_bar);
            if (state->current_tab == 2) state->pages[2] = 0;
            else state->pages[0] = state->pages[1] = 0;
            memset(state->order_pages,0,sizeof(state->order_pages));
            users_collapse_all(shop);
        }
        memcpy(input,shop->search_bar,sizeof(state->search_input));
        state->search_active = shop->search_bar_active;
        if (button_event(shop,advanced_search_button_bounds(shop))) {
            state->filter_draft = state->filters[state->current_tab];
            state->advanced_open = true;
            state->search_active = false;
            state->revealed_id = 0;
            state->error[0] = '\0';
        }
        shop->sales.search_offset = 0;
    } else {
        draw_search_bar(shop);
        draw_button(shop,advanced_search_button_bounds(shop),"Adv.",SHOP_BLUE);
    }
    memcpy(shop->search_bar,previous,sizeof(previous));
    shop->search_bar_active = active;
}

static void users_combo(const char* label, int* value, const char* const* options, int count) {
    ImGui_TextDisabled("%s",label);
    ImGui_SetNextItemWidth(-FLT_MIN);
    char id[100];
    snprintf(id,sizeof(id),"##users_%s",label);
    if (ImGui_BeginCombo(id,options[*value],0)) {
        for (int i=0;i<count;i++) if (ImGui_Selectable(options[i])) *value = i;
        ImGui_EndCombo();
    }
}

static bool users_panel_begin(const char* title, float width) {
    ImVec2 view = ImGui_GetIO()->DisplaySize;
    width = fminf(width,view.x-24);
    ImGui_SetNextWindowPosEx((ImVec2){view.x/2,view.y/2},ImGuiCond_Always,(ImVec2){0.5f,0.5f});
    ImGui_SetNextWindowSize((ImVec2){width,0},ImGuiCond_Always);
    ImGui_SetNextWindowSizeConstraints((ImVec2){width,0},(ImVec2){width,view.y-24},NULL,NULL);
    ImGui_PushStyleVarImVec2(ImGuiStyleVar_WindowPadding,(ImVec2){20,16});
    ImGui_PushStyleVarImVec2(ImGuiStyleVar_ItemSpacing,(ImVec2){12,10});
    return ImGui_Begin(title,NULL,ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoResize |
        ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoSavedSettings);
}

static void users_panel_end(void) {
    ImGui_End();
    ImGui_PopStyleVarEx(2);
}

static void users_center_buttons(float width) {
    ImGui_SetCursorPosX(ImGui_GetCursorPosX()+fmaxf(0,(ImGui_GetContentRegionAvail().x-width)/2));
}

static void users_advanced_search(Shop* shop) {
    Users_State* state = &shop->users;
    Users_Filter* filter = &state->filter_draft;
    int tab = state->current_tab;
    char* input = tab == 2 ? state->orders_search_input : state->search_input;
    char* keywords = tab == 2 ? state->orders_keywords : state->keywords;
    if (users_panel_begin("Users & Orders Advanced Search",520)) {
        ImGui_TextUnformatted(tab == 2 ? state->orders_username : tab ? "Order History" : "Users");
        sales_field("Keywords","##users_keywords",input,sizeof(state->search_input),"");
        if (tab == 0) {
            const char* roles[] = {"Any Role","User","Admin"};
            const char* statuses[] = {"Any Status","Enabled","Disabled"};
            users_combo("Role",&filter->role,roles,3);
            users_combo("Status",&filter->status,statuses,3);
        } else {
            sales_field("Customer","##customer",filter->customer,sizeof(filter->customer),"Username");
            ImGui_Checkbox("Minimum Total",&filter->use_min);
            ImGui_SameLine();ImGui_SetNextItemWidth(140);
            ImGui_BeginDisabled(!filter->use_min);
            ImGui_InputDoubleEx("##users_min",&filter->minimum,0,0,"%.2f",0);ImGui_EndDisabled();
            ImGui_Checkbox("Maximum Total",&filter->use_max);
            ImGui_SameLine();ImGui_SetNextItemWidth(140);
            ImGui_BeginDisabled(!filter->use_max);
            ImGui_InputDoubleEx("##users_max",&filter->maximum,0,0,"%.2f",0);ImGui_EndDisabled();
        }
        sales_date_picker(tab ? "Purchased on or After" : "Joined on or After","##users_from",filter->date_from,sizeof(filter->date_from),false,false);
        sales_date_picker(tab ? "Purchased on or Before" : "Joined on or Before","##users_to",filter->date_to,sizeof(filter->date_to),false,false);
        const char* user_sorts[] = {"Default (Username A-Z)","Account ID","Username","Email","Role","Status","Order Count","Total Spent","Joined Date"};
        const char* order_sorts[] = {"Default (Newest First)","Order ID","Customer","Purchase Date","Item Count","Total"};
        ImGui_Separator();
        users_combo("Sort By",&filter->sort,tab ? order_sorts : user_sorts,tab ? 6 : 9);
        ImGui_BeginDisabled(filter->sort == 0);
        bool ascending = filter->sort == 0 ? !tab : !filter->descending;
        if (ImGui_Checkbox("Ascending",&ascending) && ascending) filter->descending = false;
        ImGui_SameLine();
        bool descending = filter->sort == 0 ? tab : filter->descending;
        if (ImGui_Checkbox("Descending",&descending) && descending) filter->descending = true;
        ImGui_EndDisabled();
        if (*state->error) ImGui_TextWrapped("%s",state->error);
        ImGui_Separator();
        users_center_buttons(290);
        if (ImGui_ButtonEx("Search",(ImVec2){90,32}) && users_filter_valid(shop,filter)) {
            state->filters[tab] = *filter;
            memset(state->order_pages,0,sizeof(state->order_pages));
            snprintf(keywords,sizeof(state->keywords),"%s",input);
            if (state->current_tab == 2) state->pages[2] = 0;
            else state->pages[0] = state->pages[1] = 0;
            users_collapse_all(shop);
            state->advanced_open = false;state->error[0] = '\0';
        }
        ImGui_SameLine();
        if (ImGui_ButtonEx("Clear",(ImVec2){90,32})) {
            *filter = (Users_Filter){0};input[0] = '\0';state->error[0] = '\0';
        }
        ImGui_SameLine();
        if (ImGui_ButtonEx("Cancel",(ImVec2){90,32})) {
            snprintf(input,sizeof(state->search_input),"%s",keywords);
            state->advanced_open = false;state->error[0] = '\0';
        }
    }
    users_panel_end();
}

static void users_edit_panel(Shop* shop) {
    Users_State* state = &shop->users;
    if (users_panel_begin("Edit Account",590)) {
        ImGui_Text("Account ID: %lld",(long long)state->editing_id);
        sales_field("Username","##edit_username",state->username,sizeof(state->username),"");
        sales_field("Email (Optional)","##edit_email",state->email,sizeof(state->email),"");
        const char* roles[] = {"User","Admin"};
        int role = state->edit_role;
        users_combo("Role",&role,roles,2);
        state->edit_role = role == 1;
        ImGui_Text("Status: %s",state->original_enabled ? "Enabled" : "Disabled");
        if (*state->error) ImGui_TextWrapped("%s",state->error);
        ImGui_Separator();
        users_center_buttons(462);
        if (ImGui_ButtonEx("Save Changes",(ImVec2){140,32})) {
            if (state->edit_role != state->original_role) {
                state->confirmation = 1;
                ImGui_OpenPopup("Confirm Account Change",0);
            } else if (users_save_account(shop,false,false)) state->editing_id = 0;
        }
        ImGui_SameLine();
        if (ImGui_ButtonEx("Cancel",(ImVec2){90,32})) { state->editing_id = 0; state->error[0] = '\0'; }
        ImGui_SameLine();
        Color color = state->original_enabled ? SHOP_RED : SHOP_GREEN;
        sales_page_color(ImGuiCol_Button,color);
        sales_page_color(ImGuiCol_ButtonHovered,ColorBrightness(color,-0.5f));
        sales_page_color(ImGuiCol_ButtonActive,ColorBrightness(color,-0.8f));
        bool toggle = ImGui_ButtonEx(state->original_enabled ? "Disable Account" : "Enable Account",(ImVec2){190,32});
        ImGui_PopStyleColorEx(3);
        if (toggle) { state->confirmation = 2; ImGui_OpenPopup("Confirm Account Change",0); }
        ImVec2 view = ImGui_GetIO()->DisplaySize;
        ImGui_SetNextWindowPosEx((ImVec2){view.x/2,view.y/2},ImGuiCond_Appearing,(ImVec2){0.5f,0.5f});
        ImGui_SetNextWindowSizeConstraints((ImVec2){fminf(420,view.x-24),0},(ImVec2){fminf(520,view.x-24),view.y-24},NULL,NULL);
        bool confirmation = panel_begin_modal("Confirm Account Change",
            ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoSavedSettings);
        if (confirmation) {
            if (state->confirmation == 1) ImGui_TextWrapped("Change %s (Account ID %lld) to %s and save these account changes?",state->original_username,(long long)state->editing_id,state->edit_role ? "Admin" : "User");
            else ImGui_TextWrapped("%s %s (Account ID %lld)? %s",state->original_enabled ? "Disable" : "Enable",state->original_username,(long long)state->editing_id,state->original_enabled ? "They will be signed out and cannot sign in. Their order history will be preserved." : "They will be able to sign in again.");
            if (*state->error) ImGui_TextWrapped("%s",state->error);
            users_center_buttons(232);
            bool saved = false;
            if (ImGui_ButtonEx("Confirm",(ImVec2){110,32})) saved = users_save_account(shop,state->confirmation == 2,true);
            ImGui_SameLine();
            bool cancel = ImGui_ButtonEx("Cancel",(ImVec2){110,32});
            if (saved || cancel) {
                if (saved && state->confirmation == 1) state->editing_id = 0;
                state->confirmation = 0;
                ImGui_CloseCurrentPopup();
            }
            ImGui_EndPopup();
        }
    }
    users_panel_end();
}

static void users_columns(const char* const* columns, const float* widths, int count, bool header) {
    for (int i=0;i<count;i++) ImGui_TableSetupColumnEx(columns[i],ImGuiTableColumnFlags_WidthStretch,widths[i],0);
    if (header) sales_header_row();
}

static float users_table_width(const float* widths, int columns) {
    float width = 0;
    for (int i=0;i<columns;i++) width += widths[i]+20;
    return fmaxf(ImGui_GetContentRegionAvail().x,width);
}

static void users_row_background(int row) {
    float height = fmaxf(32,ImGui_GetFrameHeight())+ImGui_GetStyle()->CellPadding.y*2;
    ImGui_TableNextRowEx(0,height);
    ImGui_TableSetBgColor(ImGuiTableBgTarget_RowBg0,
        ImGui_GetColorU32(row%2 ? ImGuiCol_TableRowBgAlt : ImGuiCol_TableRowBg),-1);
    ImGui_TableNextColumn();
}

static void users_order_items(Shop* shop, sqlite3_int64 order_id) {
    sqlite3_stmt* stmt = users_prepare(shop,
        "SELECT subtotal,discount_amount,tax_amount,total_amount,"
        "(SELECT count(*) FROM order_items WHERE order_id=orders.id) FROM orders WHERE id=?");
    if (!stmt) return;
    sqlite3_bind_int64(stmt,1,order_id);
    if (sqlite3_step(stmt) == SQLITE_ROW) {
        ImGui_Text("Order %lld Items",(long long)order_id);
        sqlite3_stmt* items = users_prepare(shop,
            "SELECT product_name,quantity,unit_price FROM order_items WHERE order_id=? ORDER BY id");
        if (items) {
            sqlite3_bind_int64(items,1,order_id);
            const char* columns[] = {"Product","Quantity","Unit Price","Line Total"};
            const float widths[] = {270,100,140,140};
            int count = sqlite3_column_int(stmt,4);
            float width = ImGui_GetContentRegionAvail().x;
            float padding = ImGui_GetStyle()->CellPadding.y*2;
            float height = 22+padding+(ImGui_GetTextLineHeight()+padding)*MIN(5,count);
            if (count && ImGui_BeginTableEx("##order_items",4,ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingStretchProp |
                ImGuiTableFlags_ScrollY | ImGuiTableFlags_NoSavedSettings,(ImVec2){width,height},0)) {
                ImGui_TableSetupScrollFreeze(0,1);
                users_columns(columns,widths,4,true);
                while (sqlite3_step(items) == SQLITE_ROW) {
                    ImGui_TableNextRow();
                    sales_cell(sales_text(items,0));
                    users_id_cell(sqlite3_column_int64(items,1));
                    sales_money_cell(sqlite3_column_double(items,2));
                    sales_money_cell(sqlite3_column_int(items,1)*sqlite3_column_double(items,2));
                }
                ImGui_EndTable();
            }
            if (!count) ImGui_TextDisabled("No items recorded for this order.");
            sqlite3_finalize(items);
        }
        char totals[4][128];
        for (int i=0;i<4;i++) format_money(totals[i],sizeof(totals[i]),sqlite3_column_double(stmt,i));
        char summary[640];
        snprintf(summary,sizeof(summary),"Subtotal: %s   Discount: %s   Tax: %s   Total: %s",totals[0],totals[1],totals[2],totals[3]);
        float padding = ImGui_GetStyle()->CellPadding.y;
        ImGui_Dummy((ImVec2){0,padding});
        ImGui_SetCursorPosX(ImGui_GetCursorPosX()+fmaxf(0,ImGui_GetContentRegionAvail().x-ImGui_CalcTextSize(summary).x));
        ImGui_TextWrapped("%s",summary);
        ImGui_Dummy((ImVec2){0,padding});
    }
    sqlite3_finalize(stmt);
}

static void users_open_orders(Shop* shop, sqlite3_int64 id, const char* username) {
    Users_State* state = &shop->users;
    users_collapse_all(shop);
    state->orders_user_id = id;
    snprintf(state->orders_username,sizeof(state->orders_username),"%s",username);
    state->orders_search_input[0] = state->orders_keywords[0] = '\0';
    state->filters[2] = (Users_Filter){0};
    state->pages[2] = 0;
    memset(state->order_pages[1],0,sizeof(state->order_pages[1]));
    state->requested_tab = 3;
    state->scroll_to_top = true;
}

static void users_close_orders(Shop* shop) {
    Users_State* state = &shop->users;
    users_collapse_all(shop);
    state->orders_user_id = 0;
    state->current_tab = 0;
    state->requested_tab = 1;
    state->scroll_to_top = true;
}

static int users_fit_rows(Shop* shop, int tab, float inner) {
    ImGuiStyle* style = ImGui_GetStyle();
    float height = ImGui_GetContentRegionAvail().y-ImGui_GetFrameHeight()-style->ItemSpacing.y;
    float header = 22+style->CellPadding.y*2;
    float row = fmaxf(32,ImGui_GetFrameHeight())+style->CellPadding.y*2;
    if (inner > ImGui_GetContentRegionAvail().x) height -= style->ScrollbarSize;
    int size = MAX(1,(int)floorf((height-header)/row));
    if (shop->users.page_sizes[tab] != size) {
        shop->users.pages[tab] = 0;
        shop->users.page_sizes[tab] = size;
        users_collapse_all(shop);
    }
    return size;
}

static bool users_begin_rows(Shop* shop, const char* id, float width, float inner) {
    Users_State* state = &shop->users;
    float footer = ImGui_GetFrameHeight()+ImGui_GetStyle()->ItemSpacing.y;
    float height = fmaxf(1,ImGui_GetContentRegionAvail().y-footer);
    ImGui_SetNextWindowContentSize((ImVec2){inner,0});
    ImGui_SetNextWindowScroll((ImVec2){-1,state->scroll_to_top ? 0 : -1});
    bool visible = ImGui_BeginChild(id,(ImVec2){width,height},0,(inner > 0 ? ImGuiWindowFlags_HorizontalScrollbar : 0) | ImGuiWindowFlags_NoBackground);
    state->page_can_scroll = ImGui_GetScrollMaxY() > 0;
    state->scroll_y = ImGui_GetScrollY();
    float threshold = fminf(ImGui_GetWindowHeight()*0.8f,ImGui_GetScrollMaxY()*0.8f);
    state->show_back_to_top = threshold > 0 && state->scroll_y >= threshold;
    return visible;
}

static void users_order_table(Shop* shop, sqlite3_int64 user_id) {
    Users_State* state = &shop->users;
    int* page = &state->order_pages[user_id ? 1 : 0][state->orders_completed[user_id ? 1 : 0]];
    int page_size = state->page_sizes[user_id ? 2 : 1];
    sqlite3_stmt* stmt = users_query(shop,1,true,0,user_id);
    if (!stmt) return;
    int count = sqlite3_step(stmt) == SQLITE_ROW ? sqlite3_column_int(stmt,0) : 0;
    sqlite3_finalize(stmt);
    *page = MIN(*page,MAX(0,(count-1)/page_size));
    stmt = users_query(shop,1,false,*page,user_id);
    if (!stmt) return;
    const char* columns[] = {"  Order ID","Customer","Date/Time","Item Count","Total"};
    const float widths[] = {150,160,210,125,160};
    {
        int row = 0;
        ImGui_PushStyleVarImVec2(ImGuiStyleVar_ItemSpacing,(ImVec2){12,0});
        while (sqlite3_step(stmt) == SQLITE_ROW) {
            sqlite3_int64 id = sqlite3_column_int64(stmt,0), customer = sqlite3_column_int64(stmt,5);
            bool expanded = users_is_expanded(state,id,customer);
            ImGui_PushIDPtr((void*)(intptr_t)id);
            if (ImGui_BeginTable("##order_row",5,ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingStretchProp |
                ImGuiTableFlags_PadOuterX | ImGuiTableFlags_NoSavedSettings)) {
                users_columns(columns,widths,5,row == 0);
                if (!user_id && row == 0 && state->expanded_count &&
                    (ImGui_TableGetColumnFlags(4) & ImGuiTableColumnFlags_IsVisible)) {
                    ImVec2 minimum = ImGui_GetItemRectMin(), maximum = ImGui_GetItemRectMax();
                    ImGui_SetCursorScreenPos((ImVec2){maximum.x-ImGui_GetStyle()->CellPadding.x-28,
                        minimum.y+(maximum.y-minimum.y-28)*0.5f});
                    if (ImGui_ButtonEx("-##collapse_all",(ImVec2){28,28})) users_collapse_all(shop);
                    if (ImGui_IsItemHovered(0)) sales_tooltip(shop,"Collapse All");
                }
                users_row_background(row++);
                char label[48];snprintf(label,sizeof(label),"%s %lld",expanded ? "-" : "+",(long long)id);
                if (ImGui_SelectableEx(label,expanded,ImGuiSelectableFlags_SpanAllColumns | ImGuiSelectableFlags_AllowOverlap,(ImVec2){0,32}))
                    users_toggle_expanded(shop,id,customer);
                ImGui_TableNextColumn();
                if (!user_id && ImGui_Selectable(sales_text(stmt,1))) {
                    users_open_orders(shop,customer,sales_text(stmt,1));
                } else if (user_id) ImGui_TextUnformatted(sales_text(stmt,1));
                users_date_cell(stmt,2);
                users_id_cell(sqlite3_column_int64(stmt,3));
                sales_money_cell(sqlite3_column_double(stmt,4));
                ImGui_EndTable();
            }
            if (users_is_expanded(state,id,customer)) users_order_items(shop,id);
            ImGui_PopID();
        }
        ImGui_PopStyleVar();
    }
    sqlite3_finalize(stmt);
    int previous = *page;
    sales_pagination(page,count,&shop->sales,page_size);
    if (*page != previous) users_collapse_all(shop);
}

static void users_order_sections(Shop* shop, sqlite3_int64 user_id) {
    Users_State* state = &shop->users;
    users_fit_rows(shop,user_id ? 2 : 1,0);
    if (users_begin_rows(shop,"##order_sections",ImGui_GetContentRegionAvail().x,0)) {
        const char* labels[] = {"Pending","Completed"};
        for (int completed=0;completed<2;completed++) {
            state->orders_completed[user_id ? 1 : 0] = completed;
            sqlite3_stmt* stmt = users_query(shop,1,true,0,user_id);
            int count = stmt && sqlite3_step(stmt) == SQLITE_ROW ? sqlite3_column_int(stmt,0) : 0;
            sqlite3_finalize(stmt);
            char heading[64];
            snprintf(heading,sizeof(heading),"%s (%d)",labels[completed],count);
            ImGui_PushIDInt(completed);
            bool open = ImGui_CollapsingHeader(heading,ImGuiTreeNodeFlags_DefaultOpen);
            if (open) users_order_table(shop,user_id);
            ImGui_PopID();
        }
    }
    ImGui_EndChild();
}

static void users_account_table(Shop* shop) {
    Users_State* state = &shop->users;
    int* page = &state->pages[0];
    const float widths[] = {140,160,230,160,90,110,90,160,210,230};
    int page_size = users_fit_rows(shop,0,users_table_width(widths,10));
    sqlite3_stmt* stmt = users_query(shop,0,true,0,0);
    if (!stmt) return;
    int count = sqlite3_step(stmt) == SQLITE_ROW ? sqlite3_column_int(stmt,0) : 0;
    sqlite3_finalize(stmt);
    *page = MIN(*page,MAX(0,(count-1)/page_size));
    stmt = users_query(shop,0,false,*page,0);
    if (!stmt) return;
    const char* columns[] = {"Account ID","Username","Email","Password","Role","Status","Orders","Total Spent","Joined","Action"};
    float width = ImGui_GetContentRegionAvail().x;
    float inner = users_table_width(widths,10);
    {
        bool visible = users_begin_rows(shop,"##account_rows",width,inner);
        int row = 0;
        ImGui_PushStyleVarImVec2(ImGuiStyleVar_ItemSpacing,(ImVec2){12,0});
        while (visible && sqlite3_step(stmt) == SQLITE_ROW) {
            sqlite3_int64 id = sqlite3_column_int64(stmt,0);
            ImGui_PushIDPtr((void*)(intptr_t)id);
            if (ImGui_BeginTable("##account_row",10,ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingStretchProp |
                ImGuiTableFlags_PadOuterX | ImGuiTableFlags_NoSavedSettings)) {
                users_columns(columns,widths,10,row == 0);
                users_row_background(row++);
                ImGui_Text("%lld",(long long)id);
                sales_cell(sales_text(stmt,1));
                sales_cell(*sales_text(stmt,2) ? sales_text(stmt,2) : "None");
                ImGui_TableNextColumn();
                ImVec2 password_position = ImGui_GetCursorScreenPos();
                if (ImGui_SelectableEx("##password",false,0,(ImVec2){0,32}))
                    state->revealed_id = state->revealed_id == id ? 0 : id;
                ImGui_SetCursorScreenPos(password_position);
                ImGui_TextUnformatted(state->revealed_id == id ? sales_text(stmt,3) : "******");
                sales_cell(sqlite3_column_int(stmt,4) ? "Admin" : "User");
                ImGui_TableNextColumn();
                sales_page_color(ImGuiCol_Text,sqlite3_column_int(stmt,5) ? SHOP_GREEN : SHOP_TEXT_MUTED);
                ImGui_TextUnformatted(sqlite3_column_int(stmt,5) ? "Enabled" : "Disabled");
                ImGui_PopStyleColor();
                users_id_cell(sqlite3_column_int64(stmt,6));
                sales_money_cell(sqlite3_column_double(stmt,7));
                users_date_cell(stmt,8);
                ImGui_TableNextColumn();
                if (ImGui_ButtonEx(ICON_FA_PENCIL "##edit_account",(ImVec2){34,32})) users_open_edit(shop,id);
                ImGui_SameLine();
                if (ImGui_ButtonEx("View Orders",(ImVec2){145,32})) users_open_orders(shop,id,sales_text(stmt,1));
                ImGui_EndTable();
            }
            ImGui_PopID();
        }
        ImGui_PopStyleVar();
        ImGui_EndChild();
    }
    sqlite3_finalize(stmt);
    int previous = *page;
    sales_pagination(page,count,&shop->sales,page_size);
    if (*page != previous) users_collapse_all(shop);
}

static void users_dashboard(Shop* shop) {
    Users_State* state = &shop->users;
    if (!auth_has_admin_access(shop)) {
        users_collapse_all(shop);
        *state = (Users_State){.page_target=state->page_target};
        shop->screen = HOME_SCREEN;return;
    }
    Rectangle viewport = sales_viewport(shop);
    float scale = viewport.width/shop->render_target.texture.width;
    ImGui_SetNextWindowPos((ImVec2){viewport.x,viewport.y},ImGuiCond_Always);
    ImGui_SetNextWindowSize((ImVec2){viewport.width,viewport.height-100*scale},ImGuiCond_Always);
    int colors = sales_page_theme();
    ImGui_PushFontFloat(NULL,18);
    ImGui_PushStyleVarImVec2(ImGuiStyleVar_WindowPadding,(ImVec2){50,20});
    ImGui_PushStyleVarImVec2(ImGuiStyleVar_FramePadding,(ImVec2){10,7});
    ImGui_PushStyleVarImVec2(ImGuiStyleVar_ItemSpacing,(ImVec2){12,12});
    ImGui_PushStyleVarImVec2(ImGuiStyleVar_CellPadding,(ImVec2){10,8});
    ImGui_PushStyleVar(ImGuiStyleVar_WindowBorderSize,0);
    ImGui_PushStyleVar(ImGuiStyleVar_FrameRounding,5);
    ImGui_PushStyleVar(ImGuiStyleVar_ScrollbarSize,12);
    ImGui_PushStyleVar(ImGuiStyleVar_ScrollbarRounding,0);
    ImGui_PushStyleVar(ImGuiStyleVar_GrabMinSize,24);
    ImGui_SetNextWindowContentSize((ImVec2){fmaxf(1,viewport.width-100),0});
    ImGui_SetNextWindowScroll((ImVec2){0,0});
    bool blocked = state->editing_id || state->advanced_open || shop->transitioning;
    bool visible = ImGui_Begin("##users_page",NULL,ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
    if (visible) {
        ImGui_BeginDisabled(blocked);
        ImGui_Dummy((ImVec2){0,fmaxf(40,90*scale-ImGui_GetCursorPosY())});
        ImGui_Spacing();
        if (*state->message) ImGui_TextWrapped("%s",state->message);
        if (*state->error && !blocked) ImGui_TextWrapped("%s",state->error);
        if (ImGui_BeginTabBar("##users_tabs",0)) {
            const char* tabs[] = {"Users","Order History"};
            for (int tab=0;tab<2;tab++) {
                bool select = state->requested_tab == tab+1;
                if (!ImGui_BeginTabItem(tabs[tab],NULL,select ? ImGuiTabItemFlags_SetSelected : 0)) continue;
                if (state->current_tab != tab) state->revealed_id = 0;
                state->current_tab = tab;
                if (select) state->requested_tab = 0;
                if (tab == 0) users_account_table(shop);
                else users_order_sections(shop,0);
                ImGui_EndTabItem();
            }
            if (state->orders_user_id) {
                char title[160];
                snprintf(title,sizeof(title),"%s's Orders###account_orders",state->orders_username);
                bool open = true;
                if (ImGui_BeginTabItem(title,&open,state->requested_tab == 3 ? ImGuiTabItemFlags_SetSelected : 0)) {
                    if (state->current_tab != 2) state->revealed_id = 0;
                    state->current_tab = 2;state->requested_tab = 0;
                    ImGui_Text("%s's Orders",state->orders_username);
                    ImGui_SameLine();
                    ImGui_BeginDisabled(!state->expanded_count);
                    if (ImGui_ButtonEx("-##collapse_all",(ImVec2){32,32})) users_collapse_all(shop);
                    ImGui_EndDisabled();
                    if (ImGui_IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) sales_tooltip(shop,"Collapse All");
                    users_order_sections(shop,state->orders_user_id);
                    ImGui_EndTabItem();
                }
                if (!open) users_close_orders(shop);
            }
            ImGui_EndTabBar();
        }
        ImGui_EndDisabled();
    }
    ImGui_End();
    state->scroll_to_top = false;
    dashboard_back_to_top(shop,state->show_back_to_top,blocked,&state->back_to_top_alpha,&state->scroll_to_top);
    ImGui_PopStyleVarEx(9);ImGui_PopFont();ImGui_PopStyleColorEx(colors);
    if (state->editing_id) users_edit_panel(shop);
    else if (state->advanced_open) users_advanced_search(shop);
}
