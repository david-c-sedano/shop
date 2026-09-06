
#include "raylib.h"
#include "raymath.h"
#include "cimgui.h"
#include "rlImGui.h"
#include "sqlite3.h"

#include "stdio.h"

#define ARENA_IMPLEMENTATION
#include "arena.h"

#include "admin.c"

float ScaleToDPIF(float value) {
    return GetWindowScaleDPI().x * value;
}

int ScaleToDPII(int value) {
    return GetWindowScaleDPI().x * value;
}

int main(int argc, char* argv[]) {
	int screenWidth = 1280;
	int screenHeight = 800;

	SetConfigFlags(FLAG_MSAA_4X_HINT | FLAG_VSYNC_HINT | FLAG_WINDOW_RESIZABLE);
	InitWindow(screenWidth, screenHeight, "raylib-Extras [ImGui] example - simple ImGui Demo");
	SetTargetFPS(144);
	rlImGuiSetup(true);

    bool demo_window_open = false;
    Text_Editor ed;
    text_ed_init(&ed);
    Admin_Panel admin;
    admin.current_ed = &ed;

	while (!WindowShouldClose()) {
		BeginDrawing();
		ClearBackground(DARKGRAY);
		rlImGuiBegin();

        if (demo_window_open) {
		    ImGui_ShowDemoWindow(&demo_window_open);
        }

        admin_panel(&admin);

		rlImGuiEnd();
		EndDrawing();
	}

    rlImGuiShutdown();
	CloseWindow();
}
