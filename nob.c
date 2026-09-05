
#include "stdio.h"

#define NOB_IMPLEMENTATION
#include "nob.h"

int main(int argc, char **argv) {
    NOB_GO_REBUILD_URSELF(argc, argv);
    Nob_Cmd cmd = {0};

    // build raylib
    if (!nob_file_exists("./raylib/src/libraylib.a")) {
        nob_set_current_dir("./raylib/src");
        nob_cmd_append(&cmd, "make");
        if (!nob_cmd_run(&cmd)) {
            printf("\n\nfailed to build raylib!!\n");
        }
        nob_set_current_dir("../..");
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
        }
    }

    // compile ImGUI and the rendering backend into `rlImGui.o`
    if (!nob_file_exists("./rlImGui.o")) {
        nob_cmd_append(&cmd,  
            "g++",
            "-I./imgui",
            "-I./cimgui",
            "-c", "imgui_single_file.cpp", 
            "-o", "cimgui.o"
        );
        if (!nob_cmd_run(&cmd)) {
            printf("\n\nfailed to compile ImGUI!\n");
            return 1;
        }

        nob_cmd_append(&cmd,  
            "g++",
            "-I./imgui",
            "-I./raylib/src",
            "-c", "./raylib/rlImGui.cpp",
            "-o", "cimgui_backend.o"
        );
        if (!nob_cmd_run(&cmd)) {
            printf("\n\nfailed to compile ImGUI Raylib Backend!\n");
            return 1;
        }

        // link into single 'rlImGui.o'
        nob_cmd_append(&cmd,  
            "ld", "-r",
            "cimgui.o",
            "cimgui_backend.o",
            "-o", "rlImGui.o"
        );
        if (!nob_cmd_run(&cmd)) {
            printf("\n\nfailed to link rlImGui.o!\n");
            return 1;
        }
    }

    nob_cmd_append(&cmd,
        "gcc",
        "-I./imgui",
        "-I./cimgui",
        "-I./raylib",
        "-I./raylib/src",
        "-I./sqlite3",
        "rlImGui.o",
        "./src/shop.c",
        "./sqlite3/sqlite3.c",
        "-L./raylib/src",
        "-lraylib",
        "-lstdc++",
        "-lopengl32",
        "-lgdi32",
        "-lwinmm",
        "-o", "shop.exe"
    );
    if (!nob_cmd_run(&cmd)) {
        printf("\n\nfailed to build `shop.exe`!!\n");
        return 1;
    }

    //done
}
