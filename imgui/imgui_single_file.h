// hijacked by Mr Bajoding himself

#if defined(IMGUI_IMPLEMENTATION) && !defined(IMGUI_DEFINE_MATH_OPERATORS)
#define IMGUI_DEFINE_MATH_OPERATORS
#endif

#include "imgui.h"
#include "backends/imgui_impl_sdl2.h"
#include "backends/imgui_impl_opengl3.h"

#ifdef IMGUI_IMPLEMENTATION
#include "imgui.cpp"
#include "imgui_demo.cpp"
#include "imgui_draw.cpp"
#include "imgui_tables.cpp"
#include "imgui_widgets.cpp"
#include "backends/imgui_impl_sdl2.cpp"
#include "backends/imgui_impl_sdlrenderer2.cpp"
#include "backends/imgui_impl_opengl3.cpp"
#endif
