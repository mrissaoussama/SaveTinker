#pragma once

#include <imgui.h>

// Dear ImGui renderer for the 3DS GPU through citro3d. Screens are drawn tilted, as the 3DS framebuffers are
// rotated.
bool ImGui_ImplCitro3D_Init();
void ImGui_ImplCitro3D_Shutdown();
void ImGui_ImplCitro3D_RenderDrawData(ImDrawData* drawData, float screenWidth, float screenHeight);
