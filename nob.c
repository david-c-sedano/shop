#define NOB_IMPLEMENTATION
#include "nob.h"

int main(int argc, char **argv) {
    NOB_GO_REBUILD_URSELF(argc, argv);
    Nob_Cmd cmd = {0};

    nob_cmd_append(&cmd,
        "emcc",
        "-o", "sqlite3.o",
        "-c", "sqlite3/sqlite3.c",
        "-DSQLITE_OMIT_LOAD_EXTENSION"
    );
    if (!nob_cmd_run(&cmd)) return 1;

    nob_cmd_append(&cmd,
        "em++", 
        "-I./imgui",
        "-I./sqlite3",
        "shop.cpp",
        "sqlite3.o",
        "-o", "shop.js",
        "-sUSE_SDL=2"
    );
    if (!nob_cmd_run(&cmd)) return 1;

    printf("\nlink: http://localhost:8000/shop.html\n");
    nob_cmd_append(&cmd, "python", "-m", "http.server", "8000");
    if (!nob_cmd_run(&cmd)) return 1;
    return 0;
}
