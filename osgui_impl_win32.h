// osgui_impl_win32.h - platform backend (cf. imgui_impl_win32.h)
#pragma once
#include <windows.h>

bool OG_ImplWin32_Init(void* hwnd);
void OG_ImplWin32_Shutdown();
void OG_ImplWin32_NewFrame();
bool OG_ImplWin32_SetFont(const char* family, int pixel_height, int weight = FW_NORMAL);
bool OG_ImplWin32_SetFontFallback(const char* family);
const char* OG_ImplWin32_GetFontFamily();
const char* OG_ImplWin32_GetFontFallback();
int OG_ImplWin32_GetFontSize();

// Forward Win32 messages so the backend can update input state.
// Returns true if the message was consumed.
LRESULT OG_ImplWin32_WndProcHandler(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);
