// osgui_impl_opengl2.h - renderer backend (cf. imgui_impl_opengl2.h)
#pragma once
#include "osgui.h"

bool OG_ImplOpenGL2_Init();
void OG_ImplOpenGL2_Shutdown();
void OG_ImplOpenGL2_NewFrame();
void OG_ImplOpenGL2_RenderDrawData(og::DrawData* draw_data);
bool OG_ImplOpenGL2_CreateFontsTexture();
bool OG_ImplOpenGL2_HasShaderEffects();
