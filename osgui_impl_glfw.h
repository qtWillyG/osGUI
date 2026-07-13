#pragma once
#include "osgui.h"
struct GLFWwindow;

typedef bool (*OG_GlfwFontBuilder)(og::FontAtlas& atlas, float dpi_scale, void* user_data);
void OG_ImplGlfw_SetFontBuilder(OG_GlfwFontBuilder builder, void* user_data = 0);
bool OG_ImplGlfw_Init(GLFWwindow* window, bool install_callbacks = true);
void OG_ImplGlfw_Shutdown();
void OG_ImplGlfw_NewFrame();

