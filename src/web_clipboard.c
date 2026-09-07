
#ifdef PLATFORM_WEB

char* pending_paste = NULL;

const char* web_sync_clipboard_is_fricked(ImGuiContext* ctx) {
    return "";
}

static void web_set_clipboard(ImGuiContext* ctx, const char* text) {
    EM_ASM({
        const text = UTF8ToString($0);
        navigator.clipboard.writeText(text).catch(err => {
            console.error("clipboard write failed:", err);
        });
    }, text);
}

EMSCRIPTEN_KEEPALIVE
void web_paste_text(const char* text) {
    if (!text) return;
    free(pending_paste);
    pending_paste = strdup(text);
}

void web_clipboard_flush(void) {
    if (!pending_paste) {
        return;
    }
    ImGuiIO* io = ImGui_GetIO();
    if (io->KeyCtrl || io->KeySuper) {
        return;
    }
    ImGuiIO_AddInputCharactersUTF8(ImGui_GetIO(), pending_paste);
    free(pending_paste);
    pending_paste = NULL;
}

EM_JS(void, install_paste_hook, (), {
    console.log("[clipboard] installing paste hook");
    window.addEventListener("paste", function(e) {
        console.log("[clipboard] paste event fired", e);
        const text = e.clipboardData.getData("text/plain");
        console.log("[clipboard] text:", text);
        if (!text) {
            return;
        }
        
        const size = lengthBytesUTF8(text)+1;
        const ptr = _malloc(size);
        stringToUTF8(text, ptr, size);
        _web_paste_text(ptr);
        _free(ptr);
        e.preventDefault();
    });
});

#endif
