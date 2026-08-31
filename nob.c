#define NOB_IMPLEMENTATION
#include "nob.h"

int main(int argc, char **argv) {
    NOB_GO_REBUILD_URSELF(argc, argv);
    Nob_Cmd cmd = {0};
    nob_cmd_append(&cmd,
        "em++", 
        "-I./imgui",
        "shop.cpp", 
        "-o", "shop.js",
        "-sUSE_SDL=2"
    );
    if (!nob_cmd_run(&cmd)) return 1;

    printf("\nlink: http://localhost:8000/shop.html\n");
    nob_cmd_append(&cmd, "python", "-m", "http.server", "8000");
    if (!nob_cmd_run(&cmd)) return 1;
    return 0;
}
