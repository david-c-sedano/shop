
#include "stdio.h"

#define NOB_IMPLEMENTATION
#include "nob.h"

int main(int argc, char **argv) {
    NOB_GO_REBUILD_URSELF(argc, argv);
    Nob_Cmd cmd = {0};

    if (!nob_file_exists("./raylib/lib_wasm/libraylib.a")) {
        nob_set_current_dir("./raylib/src");
        nob_cmd_append(&cmd, "make", "PLATFORM=PLATFORM_WEB", "-B");
        if (!nob_cmd_run(&cmd)) {
            printf("\n\nfailed to build raylib for WASM!\n");
            return 1;
        }
        nob_set_current_dir("../..");
        nob_mkdir_if_not_exists("./raylib/lib_wasm");
        nob_rename("./raylib/src/libraylib.web.a", "./raylib/lib_wasm/libraylib.a");
    }

    // generate ImGui C API 
    if (!nob_file_exists("./cimgui")) {
        nob_mkdir_if_not_exists("cimgui");
        nob_cmd_append(&cmd,
            "python",
            "dear_bindings/dear_bindings.py",
            "--output", "cimgui/cimgui",
            "./imgui/imgui.h"
        );
        if (!nob_cmd_run(&cmd)) {
            printf("\n\nfailed to generate C API for ImGUI!\n");
            return 1;
        }
    }

    // compile ImGUI and the rendering backend
    if (!nob_file_exists("./lib_wasm")) {
        nob_mkdir_if_not_exists("./lib_wasm");

        nob_cmd_append(&cmd,
            "em++",
            "-I./imgui",
            "-I./cimgui",
            "-c", "imgui_single_file.cpp",
            "-o", "./lib_wasm/cimgui.o"
        );
        if (!nob_cmd_run(&cmd)) {
            printf("\n\nfailed to compile ImGUI for WASM!\n");
            return 1;
        }

        nob_cmd_append(&cmd,  
            "em++",
            "-DPLATFORM_WEB",
            "-I./imgui",
            "-I./raylib/src",
            "-c", "./raylib/rlImGui.cpp",
            "-o", "./lib_wasm/rlimgui.o"
        );
        if (!nob_cmd_run(&cmd)) {
            printf("\n\nfailed to compile ImGUI Raylib Backend for WASM!\n");
            return 1;
        }
    }

    nob_cmd_append(&cmd,
        "emcc",
        "-I./imgui",
        "-I./cimgui",
        "-I./raylib",
        "-I./raylib/src",
        "-I./sqlite3",
        "./sqlite3/sqlite3.c",
        "./lib_wasm/cimgui.o",
        "./lib_wasm/rlimgui.o",
        "./src/shop.c",
        "-L./raylib/lib_wasm",
        "-sUSE_GLFW=3",
        "-sASYNCIFY",
        "-sALLOW_MEMORY_GROWTH=1",
        "-lraylib",
        "-lstdc++",
        "-o", "./shop.js"
    );
    if (!nob_cmd_run(&cmd)) {
        printf("\n\nfailed to build `shop.js` and `shop.wasm`!!\n");
        return 1;
    }

    printf("\n\n\nuse: `python -m http.server 8000`\n");
    printf("then open browser and goto: localhost:8000/shop.html\n");
    //done
}
