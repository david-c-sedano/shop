
#define MIN_H 80.0
#define CELL(r, x, y) ((r)->cells[(y)*(r)->width+(x)])

typedef struct {
    char **columns,**cells;
    int width,height;
    char* error;
} SQL_Result;

typedef struct {
    char* data;
    size_t count,capacity;
    bool dirty;
    Arena alloc; 
} Text_Editor;

typedef struct {
    Text_Editor* current_ed;
    float split_h;
    sqlite3* db;
    SQL_Result prev_result; 
} Admin_Panel;

void text_ed_init(Text_Editor *ed) {
    ed->alloc = (Arena){0};
    ed->capacity = 1024;
    ed->count = 0;
    ed->dirty = false;
    ed->data = arena_alloc(&ed->alloc, ed->capacity);
    ed->data[0] = '\0';
}

int text_ed_callback(ImGuiInputTextCallbackData* cb) {
    Text_Editor* ed = cb->UserData;
    if (cb->EventFlag == ImGuiInputTextFlags_CallbackResize) {
        size_t required = (size_t)cb->BufTextLen+1;
        size_t new_cap = ed->capacity;
        while (new_cap < required) {
            new_cap *= 2;
        }
        ed->data = arena_realloc(
            &ed->alloc,
            ed->data,
            ed->capacity,
            new_cap
        );
        ed->capacity = new_cap;
        // arena_realloc can change ptrs
        cb->Buf = ed->data;
        cb->BufSize = (int)ed->capacity;
    }
    return 0;
}

// damn `gcc` wont stop complaining about 'const'
char* sql_copy_string(const char* s) {
    if (!s) s = "NULL";
    size_t n = strlen(s);
    char* out = malloc(n+1);
    memcpy(out,s,n+1);
    return out;
}

void sql_result_free(SQL_Result* r) {
    if (!r) return;
    for (int x=0; x<r->width; x++) {
        free(r->columns[x]);
    }
    for (int i=0; i<r->width*r->height; i++) {
        free(r->cells[i]);
    }
    free(r->columns);
    free(r->cells);
    free(r->error);
    *r = (SQL_Result){0};
}

SQL_Result sql_run(sqlite3* db, char* sql) {
    SQL_Result result = {0};
    if (!db) {
        result.error = sql_copy_string("where tf is the db??");
        return result;
    }

    char* error_msg = NULL;
    // SQL is alot more goated than I thought for letting u do save/restore type sh*t
    int rc = sqlite3_exec(db, "SAVEPOINT admin_run;", NULL,NULL, &error_msg);
    // paranoia
    if (rc != SQLITE_OK) {
        result.error = sql_copy_string(error_msg ? error_msg : sqlite3_errmsg(db));
        sqlite3_free(error_msg);
        return result;
    }

    const char* cursor = sql;
    while (*cursor) {
        sqlite3_stmt* stmt = NULL;
        const char* next = NULL;
        rc = sqlite3_prepare_v2(db, cursor, -1, &stmt, &next);
        if (rc != SQLITE_OK) {
            result.error = sql_copy_string(sqlite3_errmsg(db));
            goto rollback; // BUT DJISTRKA TOLD ME THIS WAS BAD!!!
        }
        if (!stmt) {
            // empty statement or comment
            cursor = next;
            continue;
        }
        int width = sqlite3_column_count(stmt);

        // if this statement gives a table, replace the previous result
        if (width > 0) {
            sql_result_free(&result);
            result.width = width;
            result.columns = calloc((size_t) width, sizeof(char*));

            for (int x=0; x<width; x++) {
                result.columns[x] = sql_copy_string(sqlite3_column_name(stmt, x));
            }
        }
        int row_cap = 0;
        while ((rc = sqlite3_step(stmt)) == SQLITE_ROW) {
            // only collect rows for statements that return columns
            if (width > 0) {
                if (result.height >= row_cap) {
                    int new_cap = row_cap ? row_cap*2 : 64;
                    char** cells = realloc(result.cells, sizeof(char*) * (size_t)(new_cap * width));

                    if (!cells) {
                        sqlite3_finalize(stmt);
                        result.error = sql_copy_string("buy more RAM, lol!");
                        goto rollback;
                    }
                    result.cells = cells;
                    row_cap = new_cap;
                }

                for (int x=0; x<width; x++) {
                    const char* text = sqlite3_column_text(stmt, x);
                    CELL(&result, x, result.height) = sql_copy_string(text ? (char*)text : "NULL");
                }

                result.height++;
            }
        }

        if (rc != SQLITE_DONE) {
            char* msg = sql_copy_string(sqlite3_errmsg(db));
            sqlite3_finalize(stmt);
            sql_result_free(&result);
            result.error = msg;
            goto rollback;
        }
        sqlite3_finalize(stmt);
        cursor = next;
    }
    
    rc = sqlite3_exec(db, "RELEASE admin_run;", NULL,NULL, &error_msg);
    // paranoia
    if (rc != SQLITE_OK) {
        sql_result_free(&result);
        result.error = sql_copy_string(error_msg ? error_msg : sqlite3_errmsg(db));
        sqlite3_free(error_msg);
    }
    return result;

rollback:
    sqlite3_exec(db, "ROLLBACK TO admin_run;", NULL,NULL,NULL);
    rc = sqlite3_exec(db, "RELEASE admin_run;", NULL,NULL,NULL);
    // last paranoia
    if (rc != SQLITE_OK) {
        sql_result_free(&result);
        result.error = sql_copy_string(error_msg ? error_msg : sqlite3_errmsg(db));
        sqlite3_free(error_msg);
    }
    return result;
}

void admin_panel(Admin_Panel* admin) {
    ImGui_Begin("Admin", NULL, ImGuiWindowFlags_NoScrollbar);

    ImVec2 avail = ImGui_GetContentRegionAvail();
    float toolbar_h = ImGui_GetFrameHeightWithSpacing();
    float splitter_h = 6.0;
    float max_editor_h = avail.y - toolbar_h - splitter_h - MIN_H;
    if (admin->split_h < MIN_H) {
        admin->split_h = MIN_H;
    }
    if (admin->split_h > max_editor_h) {
        admin->split_h = max_editor_h;
    }

    // no `using` is gonna kill me, where is Jai when you need it!!!
    Text_Editor* ed = admin->current_ed;

    /* TEXT EDITOR */
    bool dirty = ImGui_InputTextMultilineEx(
        "##editor",
        ed->data,
        ed->capacity,
        (ImVec2){ avail.x, admin->split_h },
        ImGuiInputTextFlags_AllowTabInput | ImGuiInputTextFlags_CallbackResize,
        text_ed_callback, ed
    );
    if (dirty) {
        ed->count = arena_strlen(ed->data);
        ed->dirty = true;
        // leave `dirty` as true even if `InputTextMultiline` returns false
    }

    /* HORIZONTAL SPLITTER FOR RESIZING */
    ImGui_InvisibleButton(
        "##splitter",
        (ImVec2){ avail.x, splitter_h },
        ImGuiButtonFlags_None
    );
    if (ImGui_IsItemHovered(0) || ImGui_IsItemActive()) {
        ImGui_SetMouseCursor(ImGuiMouseCursor_ResizeNS);
        // highlight the split
        ImDrawList* list = ImGui_GetWindowDrawList();
        ImDrawList_AddRectFilled(list,
            ImGui_GetItemRectMin(),
            ImGui_GetItemRectMax(),
            ImGui_GetColorU32(ImGuiCol_SeparatorHovered)
        );
        // I wonder if could just do `DrawRect` actually
    }
    if (ImGui_IsItemActive()) {
        admin->split_h += ImGui_GetIO()->MouseDelta.y;
        if (admin->split_h < MIN_H) {
            admin->split_h = MIN_H;
        }
        if (admin->split_h > max_editor_h) {
            admin->split_h = max_editor_h;
        }
    }

    /* TOOL BAR */
    if (ImGui_Button(ICON_FA_PLAY" Run")) {
        sql_result_free(&admin->prev_result);
        admin->prev_result = sql_run(admin->db, ed->data);
    }
    ImGui_SameLine();
    ImGui_TextDisabled("%zu bytes", ed->count);

    /* RESULT AREA */
    bool results = ImGui_BeginChild(
        "##results",
        (ImVec2){ 0,0 },
        ImGuiChildFlags_Borders, 0
    );
    if (results) {
        SQL_Result* r = &admin->prev_result;
        if (r->error) {
            ImGui_TextWrapped("SQL error: `%s` :(", r->error);
        } else if (r->width > 0) {
            bool table = ImGui_BeginTable(
                "##query_result",
                r->width,
                ImGuiTableFlags_Borders |
                ImGuiTableFlags_RowBg |
                ImGuiTableFlags_Resizable |
                ImGuiTableFlags_ScrollX |
                ImGuiTableFlags_ScrollY 
            );
            if (table) {
                for (int x=0; x<r->width; x++) {
                    ImGui_TableSetupColumn(r->columns[x],0);
                }
                ImGui_TableHeadersRow();
                for (int y=0; y<r->height; y++) {
                    ImGui_TableNextRow();
                    for (int x=0; x<r->width; x++) {
                        ImGui_TableSetColumnIndex(x);
                        char* value = CELL(r, x, y);
                        if (value) {
                            ImGui_TextUnformatted(value);
                        }
                    }
                }
                ImGui_EndTable();
            }
        } else {
            // TODO: put that picture of megamind
            ImGui_TextDisabled("No rows?");
        }
    }

    ImGui_EndChild();
    ImGui_End();
}
