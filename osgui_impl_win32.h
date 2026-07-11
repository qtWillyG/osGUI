// osgui_impl_win32.h - platform backend (cf. imgui_impl_win32.h)
#pragma once
#include <windows.h>

bool OG_ImplWin32_Init(void* hwnd);
void OG_ImplWin32_Shutdown();
void OG_ImplWin32_NewFrame();

// Forward Win32 messages so the backend can update input state.
// Returns true if the message was consumed.
LRESULT OG_ImplWin32_WndProcHandler(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);
