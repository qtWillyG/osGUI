#pragma once
#include "osgui.h"

// Supply glfwGetProcAddress, SDL_GL_GetProcAddress, or a WGL/GLX loader.
typedef void* (*OG_GLGetProcAddress)(const char* name);

bool OG_ImplOpenGL3_Init(OG_GLGetProcAddress loader);
void OG_ImplOpenGL3_Shutdown();
void OG_ImplOpenGL3_NewFrame();
void OG_ImplOpenGL3_RenderDrawData(og::DrawData* draw_data);
bool OG_ImplOpenGL3_CreateFontsTexture();
void OG_ImplOpenGL3_DestroyFontsTexture();
