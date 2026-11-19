// The Sales page borrows the shop's existing search control without sharing its text.
static void sales_search_bar(Shop* shop, bool update) {
    Sales_State* state = &shop->sales;
    if (update) {
        float scale = fminf((float)GetScreenWidth()/shop->render_target.texture.width,
                            (float)GetScreenHeight()/shop->render_target.texture.height);
        if (state->scroll_y >= 100.0f*scale) {
            state->search_active = false;
            return;
        }
        state->search_offset = state->scroll_y/scale;
    }
    char previous[sizeof(shop->search_bar)];
    memcpy(previous,shop->search_bar,sizeof(previous));
    bool previous_active = shop->search_bar_active;
    memcpy(shop->search_bar,state->search_input,sizeof(shop->search_bar));
    shop->search_bar_active = state->search_active;
    if (update) {
        if (search_bar_event(shop)) {
            snprintf(state->keywords,sizeof(state->keywords),"%s",shop->search_bar);
            memset(state->pages,0,sizeof(state->pages));
            state->expanded_sale = 0;
        }
        memcpy(state->search_input,shop->search_bar,sizeof(state->search_input));
        state->search_active = shop->search_bar_active;
        if (button_event(shop,advanced_search_button_bounds(shop))) {
            state->filter_draft = state->filters[state->current_tab];
            state->advanced_open = true;
            state->search_active = false;
            state->error[0] = '\0';
        }
    } else {
        draw_search_bar(shop);
        draw_button(shop,advanced_search_button_bounds(shop),"Adv.",SHOP_BLUE);
    }
    memcpy(shop->search_bar,previous,sizeof(previous));
    shop->search_bar_active = previous_active;
    state->search_offset = 0;
}

static char* sales_search_clause(Shop* shop, int tab) {
    Sales_Filter* filter = &shop->sales.filters[tab];
    SQL_Template sql = NEW_SQL;
    sql.str = malloc(sql.capacity);
    if (!sql.str) return NULL;
    APPEND_SQL(sql,"1=1");
    char* keyword = sqlite3_mprintf("%%%q%%",shop->sales.keywords);
    if (tab == 0) APPEND_SQL(sql," AND d.code LIKE '%s'",keyword);
    else if (tab == 1) APPEND_SQL(sql," AND (COALESCE(i.display,si.product_name) LIKE '%s' OR "
        "(s.kind='group' AND s.name LIKE '%s'))",keyword,keyword);
    else APPEND_SQL(sql," AND s.name LIKE '%s'",keyword);
    sqlite3_free(keyword);
    if (filter->type) {
        if (tab == 0) APPEND_SQL(sql," AND d.discount_type='%s'",filter->type == 1 ? "percent" : "fixed");
        else if (tab == 1) APPEND_SQL(sql," AND s.kind='%s'",filter->type == 1 ? "individual" : "group");
    }
    if (tab == 1 && filter->category[0]) {
        char* category = sqlite3_mprintf("%q",filter->category);
        APPEND_SQL(sql," AND i.category='%s'",category);
        sqlite3_free(category);
    }
    if (tab == 2 && filter->product[0]) {
        char* product = sqlite3_mprintf("%%%q%%",filter->product);
        APPEND_SQL(sql," AND EXISTS(SELECT 1 FROM sale_items sx LEFT JOIN items ix ON ix.id=sx.item_id "
                   "WHERE sx.sale_id=s.id AND COALESCE(ix.display,sx.product_name) LIKE '%s')",product);
        sqlite3_free(product);
    }
    const char* value = tab == 0 ? "d.discount_value" : tab == 2 ? "s.discount_value" :
        "CASE WHEN s.discount_type='percent' THEN s.discount_value ELSE "
        "100.0*(1-s.discount_value/NULLIF(CASE WHEN s.ended_at IS NOT NULL OR s.ends_at<=unixepoch() "
        "THEN si.regular_price ELSE COALESCE(i.price,si.regular_price) END,0)) END";
    if (filter->use_min) APPEND_SQL(sql," AND (%s)>=%f",value,filter->minimum);
    if (filter->use_max) APPEND_SQL(sql," AND (%s)<=%f",value,filter->maximum);
    const char* start = "date(s.starts_at,'unixepoch','localtime')";
    const char* end = tab == 0 ? "date(d.expiration_date)" : "date(COALESCE(s.ended_at,s.ends_at),'unixepoch','localtime')";
    const char* dates[] = {filter->start_from,filter->start_to,filter->end_from,filter->end_to};
    for (int n = tab == 0 ? 2 : 0; n < 4; n++) {
        if (!dates[n][0]) continue;
        char* date = sqlite3_mprintf("%q",dates[n]);
        APPEND_SQL(sql," AND %s %s '%s'",n < 2 ? start : end,n%2 ? "<=" : ">=",date);
        sqlite3_free(date);
    }
    return sql.str;
}

static char* sales_sort_order(Shop* shop, int tab) {
    Sales_Filter* filter = &shop->sales.filters[tab];
    const char* codes[] = {NULL,"d.code COLLATE NOCASE","d.discount_value","CASE WHEN length(d.expiration_date)=10 THEN datetime(d.expiration_date,'+1 day') ELSE datetime(d.expiration_date) END","d.created_at"};
    const char* groups[] = {NULL,"s.name COLLATE NOCASE","s.discount_value",
        "(SELECT count(*) FROM sale_items WHERE sale_id=s.id)","s.starts_at","COALESCE(s.ended_at,s.ends_at)"};
    const char* items[] = {NULL,"COALESCE(i.display,si.product_name) COLLATE NOCASE",NULL,NULL,NULL,"s.starts_at","COALESCE(s.ended_at,s.ends_at)"};
    int count = tab == 0 ? 5 : tab == 1 ? 7 : 6;
    int sort = filter->sort;
    if (sort <= 0 || sort >= count) return sqlite3_mprintf("%s",tab == 0 ? "d.id DESC" : tab == 1 ? "s.starts_at DESC,s.id DESC,si.item_id ASC" : "s.starts_at DESC,s.id DESC");
    const char* column = tab == 0 ? codes[sort] : tab == 2 ? groups[sort] : items[sort];
    char* price = NULL;
    if (tab == 1 && sort >= 2 && sort <= 4) {
        const char* regular = "CASE WHEN s.ended_at IS NOT NULL OR s.ends_at<=unixepoch() OR i.id IS NULL THEN si.regular_price ELSE i.price END";
        price = sqlite3_mprintf("CASE WHEN s.ended_at IS NOT NULL OR s.ends_at<=unixepoch() OR i.id IS NULL THEN si.sale_price "
            "WHEN s.discount_type='percent' THEN round((%s)*(1-s.discount_value/100.0),2) ELSE s.discount_value END",regular);
        char* order = sqlite3_mprintf(sort == 2 ? "%s" : sort == 3 ? "%s" : "100.0*(1-(%s)/NULLIF((%s),0))",
                                     sort == 2 ? regular : price,regular);
        sqlite3_free(price);
        char* result = sqlite3_mprintf("(%s) IS NULL,%s %s,s.id DESC,si.item_id ASC",order,order,filter->descending ? "DESC" : "ASC");
        sqlite3_free(order);
        return result;
    }
    return sqlite3_mprintf("(%s) IS NULL,%s %s,%s DESC%s",column,column,filter->descending ? "DESC" : "ASC",
                          tab == 0 ? "d.id" : "s.id",tab == 1 ? ",si.item_id ASC" : "");
}

static bool sales_filter_valid(Shop* shop, Sales_Filter* filter) {
    if ((filter->use_min && (!isfinite(filter->minimum) || filter->minimum < 0)) ||
        (filter->use_max && (!isfinite(filter->maximum) || filter->maximum < 0)) ||
        (filter->use_min && filter->use_max && filter->minimum > filter->maximum))
        return sales_error(shop,"Enter a valid discount range with the minimum below the maximum.");
    const char* dates[] = {filter->start_from,filter->start_to,filter->end_from,filter->end_to};
    sqlite3_int64 stamp;
    for (int i = 0; i < 4; i++)
        if (dates[i][0] && !sales_parse_local(dates[i],true,&stamp))
            return sales_error(shop,"Enter dates as YYYY-MM-DD or leave them blank.");
    for (int i = 0; i < 4; i += 2)
        if (dates[i][0] && dates[i+1][0] && strcmp(dates[i],dates[i+1]) > 0)
            return sales_error(shop,"Each ending date must be on or after its starting date.");
    return true;
}

static int sales_month_days(int year, int month) {
    const int days[] = {31,28,31,30,31,30,31,31,30,31,30,31};
    return days[month-1]+(month == 2 && year%4 == 0 && (year%100 != 0 || year%400 == 0));
}

static bool sales_parse_clock(const char* text, bool pm, int* hour, int* minute) {
    size_t length = strlen(text);
    if ((length != 4 && length != 5) || text[length-3] != ':') return false;
    for (size_t i = 0; i < length; i++)
        if (i != length-3 && !isdigit((unsigned char)text[i])) return false;
    int h, m;
    if (sscanf(text,"%2d:%2d",&h,&m) != 2 || h < 1 || h > 12 || m < 0 || m > 59) return false;
    *hour = h%12+(pm ? 12 : 0);
    *minute = m;
    return true;
}

static void sales_date_picker(const char* label, const char* id, char* date, size_t capacity, bool with_time, bool utc) {
    static int year, month, hour, minute;
    static char selected[11], clock[16];
    static bool pm, invalid_time;
    ImGui_PushID(id);
    ImGui_TextDisabled("%s",label);
    char preview[64];
    snprintf(preview,sizeof(preview),ICON_FA_CALENDAR "  %s",*date ? date : with_time ? (utc ? "No Expiration" : "Choose Date and Time") : "Any Date");
    if (ImGui_ButtonEx(preview,(ImVec2){ImGui_GetContentRegionAvail().x,0})) ImGui_OpenPopup("Calendar",0);
    ImVec2 field = ImGui_GetItemRectMin(), bottom = ImGui_GetItemRectMax(), view = ImGui_GetIO()->DisplaySize;
    ImGui_SetNextWindowPos((ImVec2){Clamp(field.x,12,fmaxf(12,view.x-300)),
                                  Clamp(bottom.y+4,12,fmaxf(12,view.y-(with_time ? 360 : 280)))},ImGuiCond_Appearing);
    ImGui_PushStyleVarImVec2(ImGuiStyleVar_ItemSpacing,(ImVec2){8,4});
    if (ImGui_BeginPopup("Calendar",ImGuiWindowFlags_NoSavedSettings)) {
        if (ImGui_IsWindowAppearing()) {
            time_t now = time(NULL);
            struct tm today = *(utc ? gmtime(&now) : localtime(&now));
            year = today.tm_year+1900;
            month = today.tm_mon+1;
            strftime(selected,sizeof(selected),"%Y-%m-%d",&today);
            hour = 23; minute = 59;
            if (*date) {
                snprintf(selected,sizeof(selected),"%.10s",date);
                sscanf(date,"%d-%d",&year,&month);
                if (strlen(date) == 16) sscanf(date+11,"%d:%d",&hour,&minute);
            }
            pm = hour >= 12;
            invalid_time = false;
            snprintf(clock,sizeof(clock),"%02d:%02d",hour%12 ? hour%12 : 12,minute);
        }
        const char* months[] = {"January","February","March","April","May","June","July","August","September","October","November","December"};
        ImGui_BeginDisabled(year == 1970 && month == 1);
        if (ImGui_Button("<")) { if (--month == 0) { month = 12; year--; } }
        ImGui_EndDisabled();
        ImGui_SameLine();
        ImGui_SetNextItemWidth(110);
        if (ImGui_BeginCombo("##month",months[month-1],0)) {
            for (int m = 1; m <= 12; m++) if (ImGui_Selectable(months[m-1])) month = m;
            ImGui_EndCombo();
        }
        ImGui_SameLine();
        ImGui_SetNextItemWidth(90);
        if (ImGui_BeginCombo("##year",TextFormat("%d",year),0)) {
            ImGuiListClipper years = {0};
            ImGuiListClipper_Begin(&years,9999-1970+1,-1);
            ImGuiListClipper_IncludeItemByIndex(&years,year-1970);
            while (ImGuiListClipper_Step(&years)) {
                for (int i = years.DisplayStart; i < years.DisplayEnd; i++) {
                    bool selected = year == 1970+i;
                    if (ImGui_SelectableEx(TextFormat("%d",1970+i),selected,0,(ImVec2){0})) year = 1970+i;
                    if (selected) ImGui_SetItemDefaultFocus();
                }
            }
            ImGui_EndCombo();
        }
        ImGui_SameLine();
        ImGui_BeginDisabled(year == 9999 && month == 12);
        if (ImGui_Button(">")) { if (++month == 13) { month = 1; year++; } }
        ImGui_EndDisabled();
        const char* weekdays[] = {"Su","Mo","Tu","We","Th","Fr","Sa"};
        const int offsets[] = {0,3,2,5,0,3,5,1,4,6,2,4};
        int y = year-(month < 3);
        int first = (y+y/4-y/100+y/400+offsets[month-1]+1)%7;
        if (ImGui_BeginTable("##days",7,ImGuiTableFlags_SizingFixedSame)) {
            for (int d = 0; d < 7; d++) { ImGui_TableNextColumn(); ImGui_TextUnformatted(weekdays[d]); }
            int days = sales_month_days(year,month);
            for (int cell = 0; cell < first+days; cell++) {
                ImGui_TableNextColumn();
                if (cell < first) continue;
                char value[16];
                snprintf(value,sizeof(value),"%04d-%02d-%02d",year,month,cell-first+1);
                char day[4];
                snprintf(day,sizeof(day),"%d",cell-first+1);
                if (ImGui_SelectableEx(day,strcmp(with_time ? selected : date,value) == 0,
                    with_time ? ImGuiSelectableFlags_NoAutoClosePopups : 0,(ImVec2){28,24})) {
                    if (with_time) snprintf(selected,sizeof(selected),"%s",value);
                    else { snprintf(date,capacity,"%s",value); ImGui_CloseCurrentPopup(); }
                }
            }
            ImGui_EndTable();
        }
        if (with_time) {
            ImGui_Separator();
            ImGui_Text("%s (%s)",selected,utc ? "UTC" : "Local Time");
            ImGui_SetNextItemWidth(160);
            if (ImGui_InputTextWithHint("##time","hh:mm",clock,sizeof(clock),0)) invalid_time = false;
            ImGui_SameLine();
            ImGui_SetNextItemWidth(80);
            if (ImGui_BeginCombo("##period",pm ? "PM" : "AM",0)) {
                if (ImGui_Selectable("AM")) pm = false;
                if (ImGui_Selectable("PM")) pm = true;
                ImGui_EndCombo();
            }
            if (invalid_time) ImGui_TextWrapped("Enter a time from 1:00 to 12:59.");
            if (ImGui_Button("Apply")) {
                invalid_time = !sales_parse_clock(clock,pm,&hour,&minute);
                if (!invalid_time) {
                    snprintf(date,capacity,"%s %02d:%02d",selected,hour,minute);
                    ImGui_CloseCurrentPopup();
                }
            }
            ImGui_SameLine();
            if (ImGui_Button("Cancel")) ImGui_CloseCurrentPopup();
        }
        ImGui_Separator();
        if (ImGui_Button("Today")) {
            time_t now = time(NULL);
            if (with_time) {
                struct tm today = *(utc ? gmtime(&now) : localtime(&now));
                strftime(selected,sizeof(selected),"%Y-%m-%d",&today);
                year = today.tm_year+1900; month = today.tm_mon+1;
            } else { strftime(date,capacity,"%Y-%m-%d",utc ? gmtime(&now) : localtime(&now)); ImGui_CloseCurrentPopup(); }
        }
        ImGui_SameLine();
        if (ImGui_Button("Clear")) { date[0] = '\0'; ImGui_CloseCurrentPopup(); }
        ImGui_EndPopup();
    }
    ImGui_PopStyleVar();
    ImGui_PopID();
}

static void sales_advanced_search(Shop* shop) {
    Sales_State* state = &shop->sales;
    Sales_Filter* filter = &state->filter_draft;
    int tab = state->current_tab;
    ImVec2 view = ImGui_GetIO()->DisplaySize;
    ImGui_SetNextWindowPosEx((ImVec2){view.x/2,view.y/2},ImGuiCond_Always,(ImVec2){0.5f,0.5f});
    float width = fminf(520,view.x-24);
    ImGui_SetNextWindowSize((ImVec2){width,0},ImGuiCond_Always);
    ImGui_SetNextWindowSizeConstraints((ImVec2){width,0},(ImVec2){width,view.y-24},NULL,NULL);
    ImGui_PushStyleVarImVec2(ImGuiStyleVar_WindowPadding,(ImVec2){20,16});
    ImGui_PushStyleVarImVec2(ImGuiStyleVar_ItemSpacing,(ImVec2){12,10});
    if (ImGui_Begin("Sales Advanced Search",NULL,ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoResize |
        ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoSavedSettings)) {
        const char* tabs[] = {"Discount Codes","Items on Sale","Group Sales"};
        ImGui_TextUnformatted(tabs[tab]);
        sales_field("Keywords", "##sales_keywords",state->search_input,sizeof(state->search_input),"");
        ImGui_TextDisabled(tab == 0 ? "Expiration dates follow checkout's UTC dates." : "Start and end dates use this computer's local time.");
        if (tab != 2) {
            const char* options[] = {"Any",tab == 0 ? "Percentage" : "Individual",tab == 0 ? "Dollar Amount" : "Group"};
            ImGui_TextDisabled("Discount Type");
            ImGui_SetNextItemWidth(-FLT_MIN);
            if (ImGui_BeginCombo("##filter_type",options[filter->type],0)) {
                for (int i = 0; i < 3; i++) if (ImGui_Selectable(options[i])) filter->type = i;
                ImGui_EndCombo();
            }
        }
        if (tab == 1) {
            const char* values[] = {"",ITEM_CATEGORY_CHAIRS,ITEM_CATEGORY_FURNITURE,ITEM_CATEGORY_LARGE};
            const char* labels[] = {"Any Category","Chairs","Other Furnishings","Larger Items"};
            int selected = 0;
            for (int i = 1; i < 4; i++) if (strcmp(filter->category,values[i]) == 0) selected = i;
            ImGui_TextDisabled("Category");
            ImGui_SetNextItemWidth(-FLT_MIN);
            if (ImGui_BeginCombo("##filter_category",labels[selected],0)) {
                for (int i = 0; i < 4; i++)
                    if (ImGui_Selectable(labels[i])) snprintf(filter->category,sizeof(filter->category),"%s",values[i]);
                ImGui_EndCombo();
            }
        }
        if (tab == 2) sales_field("Included Product", "##filter_product",filter->product,sizeof(filter->product),"Product Name");
        ImGui_Separator();
        ImGui_TextUnformatted("Sort");
        const char* code_sorts[] = {"Default (Newest First)","Code","Discount Value","Expiration Date","Created Date"};
        const char* item_sorts[] = {"Default (Newest First)","Product","Regular Price","Sale Price","Percentage Off","Start Date","End Date"};
        const char* group_sorts[] = {"Default (Newest First)","Sale Name","Percentage Off","Item Count","Start Date","End Date"};
        const char* const* sorts = tab == 0 ? code_sorts : tab == 1 ? item_sorts : group_sorts;
        int sort_count = tab == 0 ? 5 : tab == 1 ? 7 : 6;
        ImGui_SetNextItemWidth(-FLT_MIN);
        if (ImGui_BeginCombo("##sales_sort",sorts[filter->sort],0)) {
            for (int i = 0; i < sort_count; i++) if (ImGui_Selectable(sorts[i])) filter->sort = i;
            ImGui_EndCombo();
        }
        ImGui_BeginDisabled(filter->sort == 0);
        bool ascending = filter->sort != 0 && !filter->descending;
        if (ImGui_Checkbox("Ascending",&ascending) && ascending) filter->descending = false;
        ImGui_SameLine();
        bool descending = filter->sort == 0 || filter->descending;
        if (ImGui_Checkbox("Descending",&descending) && descending) filter->descending = true;
        ImGui_EndDisabled();
        ImGui_Separator();
        ImGui_TextDisabled(tab == 0 ? "Discount Value" : "Percentage Off");
        ImGui_Checkbox("Minimum",&filter->use_min); ImGui_SameLine();
        ImGui_BeginDisabled(!filter->use_min);ImGui_SetNextItemWidth(160);ImGui_InputDoubleEx("##filter_min",&filter->minimum,0,0,"%.2f",0);ImGui_EndDisabled();
        ImGui_Checkbox("Maximum",&filter->use_max); ImGui_SameLine();
        ImGui_BeginDisabled(!filter->use_max);ImGui_SetNextItemWidth(160);ImGui_InputDoubleEx("##filter_max",&filter->maximum,0,0,"%.2f",0);ImGui_EndDisabled();
        if (tab != 0) {
            sales_date_picker("Starts on or After", "##start_from",filter->start_from,sizeof(filter->start_from),false,false);
            sales_date_picker("Starts on or Before", "##start_to",filter->start_to,sizeof(filter->start_to),false,false);
        }
        sales_date_picker(tab == 0 ? "Expires on or After" : "Ends on or After", "##end_from",filter->end_from,sizeof(filter->end_from),false,tab == 0);
        sales_date_picker(tab == 0 ? "Expires on or Before" : "Ends on or Before", "##end_to",filter->end_to,sizeof(filter->end_to),false,tab == 0);
        if (state->error[0]) ImGui_TextWrapped("%s",state->error);
        ImGui_Separator();
        if (ImGui_Button("Search") && sales_filter_valid(shop,filter)) {
            state->filters[tab] = *filter;
            snprintf(state->keywords,sizeof(state->keywords),"%s",state->search_input);
            memset(state->pages,0,sizeof(state->pages));
            state->expanded_sale = 0;
            state->advanced_open = false;
            state->error[0] = '\0';
        }
        ImGui_SameLine();
        if (ImGui_Button("Clear")) {
            *filter = (Sales_Filter){0};
            state->search_input[0] = '\0';
            state->error[0] = '\0';
        }
        ImGui_SameLine();
        bool cancel = ImGui_Button("Cancel");
        cancel |= ImGui_IsItemHovered(ImGuiHoveredFlags_AllowWhenBlockedByPopup) && ImGui_IsMouseClicked(0);
        if (cancel) {
            ImGui_SetWindowFocus();
            snprintf(state->search_input,sizeof(state->search_input),"%s",state->keywords);
            state->advanced_open = false;
            state->error[0] = '\0';
        }
    }
    ImGui_End();
    ImGui_PopStyleVarEx(2);
}
