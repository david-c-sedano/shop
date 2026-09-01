#include <SDL.h>
#include <SDL_opengl.h>
#include <emscripten.h>
#include <string>
#include <vector>

#define IMGUI_IMPLEMENTATION
#include "imgui_single_file.h"

#include "sqlite3.h"

SDL_Window* g_Window = nullptr;
SDL_GLContext g_GLContext = nullptr;

void main_loop(void* arg);
void admin_prompt();

int main(int, char**) {
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_TIMER | SDL_INIT_GAMECONTROLLER) != 0) {
        return -1;
    }

    SDL_GL_SetAttribute(SDL_GL_CONTEXT_FLAGS, 0);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_ES);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 2);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 0);

    SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
    SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 24);
    SDL_GL_SetAttribute(SDL_GL_STENCIL_SIZE, 8);
    g_Window = SDL_CreateWindow("ImGui WASM", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, 1280, 720, SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE | SDL_WINDOW_ALLOW_HIGHDPI);
    g_GLContext = SDL_GL_CreateContext(g_Window);

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();

    ImGui_ImplSDL2_InitForOpenGL(g_Window, g_GLContext);
    ImGui_ImplOpenGL3_Init("#version 100");

    emscripten_set_main_loop_arg(main_loop, nullptr, 0, true);

    return 0;
}

void main_loop(void* arg) {
    ImGuiIO& io = ImGui::GetIO();
    
    SDL_Event event;
    while (SDL_PollEvent(&event)) {
        ImGui_ImplSDL2_ProcessEvent(&event);
    }

    ImGui_ImplOpenGL3_NewFrame();
    ImGui_ImplSDL2_NewFrame();
    ImGui::NewFrame();

    admin_prompt();

    ImGui::Render();
    glViewport(0, 0, (int)io.DisplaySize.x, (int)io.DisplaySize.y);
    glClearColor(0.45f, 0.55f, 0.60f, 1.00f);
    glClear(GL_COLOR_BUFFER_BIT);
    ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
    
    SDL_GL_SwapWindow(g_Window);
}

std::string sql_error;
std::vector<std::string> columns;
std::vector<std::vector<std::string>> rows;

char buf[4000] = "SELECT name, price_cents, quantity FROM items;";

void admin_prompt() {
    ImGui::Begin("Admin Console");

    ImGui::InputText("Label", buf, IM_ARRAYSIZE(buf));

    bool run =  ImGui::Button("Run SQL") 
             || (ImGui::IsKeyDown(ImGuiMod_Ctrl) && ImGui::IsKeyPressed(ImGuiKey_Enter));
    ImGui::SameLine();
    ImGui::End();
}
