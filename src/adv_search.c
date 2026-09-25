
void adv_search_panel(Adv_Search_Panel *adv_search) {
    ImGui_SetNextWindowSizeConstraints(
        (ImVec2){400.0,500.0}, 
        (ImVec2){FLT_MAX, FLT_MAX}, 
        NULL,NULL
    );
    bool visible = ImGui_Begin("Advanced Search", &adv_search->active, ImGuiWindowFlags_NoCollapse);
    if (visible) {
    }
    ImGui_End();
}
