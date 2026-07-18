#pragma once
#include "osgui.h"
struct GLFWwindow;

typedef bool (*OG_GlfwFontBuilder)(og::FontAtlas& atlas, float dpi_scale, void* user_data);
// The builder must allocate atlas.pixels with malloc-compatible ownership.
// OSGui keeps the previous atlas if rebuilding fails and frees successful
// builder output during the next rebuild/platform shutdown.
void OG_ImplGlfw_SetFontBuilder(OG_GlfwFontBuilder builder, void* user_data = 0);
bool OG_ImplGlfw_Init(GLFWwindow* window, bool install_callbacks = true);
void OG_ImplGlfw_Shutdown();
void OG_ImplGlfw_NewFrame();
