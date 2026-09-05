
#include "raylib.h"
#include "raymath.h"
#include "cimgui.h"
#include "rlImGui.h"

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

	while (!WindowShouldClose()) {
		BeginDrawing();
		ClearBackground(DARKGRAY);

		rlImGuiBegin();

		bool open = true;
		ImGui_ShowDemoWindow(&open);

		open = true;
		if (ImGui_Begin("Test Window", &open, 0))
		{
			ImGui_TextUnformatted(ICON_FA_JEDI);

		}
		ImGui_End();

		// end ImGui Content
		rlImGuiEnd();

		if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT))
			DrawText("Prssed", 0, 0, 20, RED);

		if (IsMouseButtonDown(MOUSE_BUTTON_LEFT))
			DrawText("Down", 0, 20, 20, GREEN);

		if (IsWindowFocused())
			DrawText("Focused", 100, 20, 20, WHITE);

		EndDrawing();
		//----------------------------------------------------------------------------------
	}

	// De-Initialization
	//--------------------------------------------------------------------------------------   
    rlImGuiShutdown();
	CloseWindow();        // Close window and OpenGL context
	//--------------------------------------------------------------------------------------

	return 0;
}
