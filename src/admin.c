
#define MIN_H 80.0

typedef struct {
    char* data;
    size_t count;
    size_t capacity;
    bool dirty;
    Arena alloc; 
} Text_Editor;

typedef struct {
    Text_Editor* current_ed;
    float editor_h;
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

void admin_panel(Admin_Panel* admin) {
    ImGui_Begin("Admin", NULL, ImGuiWindowFlags_NoScrollbar);

    ImVec2 avail = ImGui_GetContentRegionAvail();
    float toolbar_h = ImGui_GetFrameHeightWithSpacing();
    float splitter_h = 6.0;
    float max_editor_h = avail.y - toolbar_h - splitter_h - MIN_H;
    if (admin->editor_h < MIN_H) {
        admin->editor_h = MIN_H;
    }
    if (admin->editor_h > max_editor_h) {
        admin->editor_h = max_editor_h;
    }

    // no `using` is gonna kill me, where is Jai when you need it!!!
    Text_Editor* ed = admin->current_ed;

    /* TEXT EDITOR */
    bool dirty = ImGui_InputTextMultilineEx(
        "##editor",
        ed->data,
        ed->capacity,
        (ImVec2){ avail.x, admin->editor_h },
        ImGuiInputTextFlags_AllowTabInput | ImGuiInputTextFlags_CallbackResize,
        text_ed_callback, ed
    );
    if (dirty) {
        ed->count = arena_strlen(ed->data);
        ed->dirty = true;
        // leave `dirty` as true even if `InputTextMultiline` returns false
    }

    /* SPLITTER FOR RESIZING */
    ImGui_InvisibleButton(
        "##splitter",
        (ImVec2){ avail.x, splitter_h },
        ImGuiButtonFlags_None
    );
    if (ImGui_IsItemHovered(0) || ImGui_IsItemActive()) {
        ImGui_SetMouseCursor(ImGuiMouseCursor_ResizeNS);
    }
    if (ImGui_IsItemActive()) {
        admin->editor_h += ImGui_GetIO()->MouseDelta.y;
        if (admin->editor_h < MIN_H) {
            admin->editor_h = MIN_H;
        }
        if (admin->editor_h > max_editor_h) {
            admin->editor_h = max_editor_h;
        }
    }
    float table_h = avail.y - admin->editor_h - splitter_h - toolbar_h;

    /* TOOL BAR */
    if (ImGui_Button(ICON_FA_PLAY" Run")) {
        //do_thing(..);
    }
    ImGui_SameLine();
    if (ImGui_Button(ICON_FA_WAND_MAGIC_SPARKLES" Format")) {
        //do_thing(..);
    }
    ImGui_SameLine();
    ImGui_TextDisabled("%zu bytes", ed->count);

    /* RESULT AREA */
    // TODO: this is dummy rn and doesnt even show columns, but thats fine for now
    bool results = ImGui_BeginChild(
        "##results",
        (ImVec2){ avail.x, table_h },
        ImGuiChildFlags_Borders, 0
    );
    if (results) {
        bool table = ImGui_BeginTable(
            "##query_result",
            3,
            ImGuiTableFlags_Borders |
            ImGuiTableFlags_RowBg |
            ImGuiTableFlags_Resizable |
            ImGuiTableFlags_ScrollX |
            ImGuiTableFlags_ScrollY
        );
        if (table) {
            ImGui_TableSetupColumn("id",0);
            ImGui_TableSetupColumn("name",0);
            ImGui_TableSetupColumn("price",0);

            for (int i=0; i<100; i++) {
                ImGui_TableNextRow();
                ImGui_TableSetColumnIndex(0);
                ImGui_Text("%d", i);
                ImGui_TableSetColumnIndex(1);
                ImGui_Text("thingymabob #%d", i);
                ImGui_TableSetColumnIndex(2);
                ImGui_Text("%d dollars", i);
            }

            ImGui_EndTable();
        }
    }

    ImGui_EndChild();
    ImGui_End();
}
