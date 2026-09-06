
#include "stdio.h"

#define NOB_IMPLEMENTATION
#include "nob.h"

int main(int argc, char **argv) {
    NOB_GO_REBUILD_URSELF(argc, argv);
    Nob_Cmd cmd = {0};

    // build raylib
    if (!nob_file_exists("./raylib/lib/libraylib.a")) {
        nob_set_current_dir("./raylib/src");
        nob_cmd_append(&cmd, "make");
        if (!nob_cmd_run(&cmd)) {
            printf("\n\nfailed to build raylib!!\n");
            return 1;
        }
        
        nob_set_current_dir("../..");
        nob_mkdir_if_not_exists("./raylib/lib");
        nob_rename("./raylib/src/libraylib.a", "./raylib/lib/libraylib.a");
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
    if (!nob_file_exists("./lib")) {
        nob_mkdir_if_not_exists("./lib");

        nob_cmd_append(&cmd,  
            "g++",
            "-I./imgui",
            "-I./cimgui",
            "-c", "imgui_single_file.cpp", 
            "-o", "./lib/cimgui.o"
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
            "-o", "./lib/rlimgui.o"
        );
        if (!nob_cmd_run(&cmd)) {
            printf("\n\nfailed to compile ImGUI Raylib Backend!\n");
            return 1;
        }
    }

    // cache SQL so im not recompiling a quarter million lines to change an icon
    if (!nob_file_exists("./lib/sqlite3.o")) {
        nob_cmd_append(&cmd,  
            "gcc",
            "-c", "./sqlite3/sqlite3.c", 
            "-o", "./lib/sqlite3.o"
        );
        if (!nob_cmd_run(&cmd)) {
            printf("\n\nfailed to compile sqlite3!\n");
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
        "./lib/sqlite3.o",
        "./lib/cimgui.o",
        "./lib/rlimgui.o",
        "./src/shop.c",
        "-L./raylib/lib",
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
