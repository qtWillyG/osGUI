// OSGui example host: Win32 + dependency-free legacy OpenGL.
#include "osgui.h"
#include "osgui_impl_win32.h"
#include "osgui_impl_opengl2.h"
#include <windows.h>
#include <GL/gl.h>

static HGLRC g_hRC = 0;
static HDC   g_hDC = 0;

static bool CreateGLContext(HWND hwnd) {
    g_hDC = GetDC(hwnd);
    PIXELFORMATDESCRIPTOR pfd;
    memset(&pfd, 0, sizeof(pfd));
    pfd.nSize = sizeof(pfd);
    pfd.nVersion = 1;
    pfd.dwFlags = PFD_DRAW_TO_WINDOW | PFD_SUPPORT_OPENGL | PFD_DOUBLEBUFFER;
    pfd.iPixelType = PFD_TYPE_RGBA;
    pfd.cColorBits = 32;
    pfd.cDepthBits = 24;
    pfd.iLayerType = PFD_MAIN_PLANE;
    int pf = ChoosePixelFormat(g_hDC, &pfd);
    if (!pf) return false;
    if (!SetPixelFormat(g_hDC, pf, &pfd)) return false;
    g_hRC = wglCreateContext(g_hDC);
    if (!g_hRC) return false;
    wglMakeCurrent(g_hDC, g_hRC);
    return true;
}

static void DestroyGLContext(HWND hwnd) {
    wglMakeCurrent(0, 0);
    if (g_hRC) wglDeleteContext(g_hRC);
    if (g_hDC) ReleaseDC(hwnd, g_hDC);
    g_hRC = 0; g_hDC = 0;
}

static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    if (OG_ImplWin32_WndProcHandler(hwnd, msg, wParam, lParam))
        return DefWindowProc(hwnd, msg, wParam, lParam);
    switch (msg) {
    case WM_SIZE:
        if (g_hRC) glViewport(0, 0, LOWORD(lParam), HIWORD(lParam));
        return 0;
    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProc(hwnd, msg, wParam, lParam);
}

int WINAPI WinMain(HINSTANCE hInst, HINSTANCE, LPSTR, int nCmdShow) {
    // Enables crisp physical-pixel rendering; the backend tracks per-monitor DPI.
    typedef BOOL (WINAPI* SetDpiContextFn)(HANDLE);
    SetDpiContextFn set_dpi = (SetDpiContextFn)GetProcAddress(GetModuleHandleA("user32.dll"), "SetProcessDpiAwarenessContext");
    if (set_dpi) set_dpi((HANDLE)-4); else SetProcessDPIAware();
    WNDCLASSEXA wc;
    memset(&wc, 0, sizeof(wc));
    wc.cbSize = sizeof(wc);
    wc.style = CS_HREDRAW | CS_VREDRAW | CS_OWNDC;
    wc.lpfnWndProc = WndProc;
    wc.hInstance = hInst;
    wc.hCursor = LoadCursor(0, IDC_ARROW);
    wc.lpszClassName = "osGUIWindow";
    RegisterClassExA(&wc);

    HWND hwnd = CreateWindowA("osGUIWindow", "OSGui - Modern C++ Interface",
                              WS_OVERLAPPEDWINDOW, 30, 30, 1440, 900,
                              0, 0, hInst, 0);
    if (!CreateGLContext(hwnd)) { MessageBoxA(0, "Failed to create OpenGL context", "osGUI", MB_OK); return 1; }

    ShowWindow(hwnd, nCmdShow);
    UpdateWindow(hwnd);

    og::CreateContext();
    OG_ImplWin32_Init(hwnd);
    OG_ImplOpenGL2_Init();

    bool show_demo = true;
    bool show_nodes = true;
    bool show_theme_editor = true;

    bool running = true;
    while (running) {
        MSG msg;
        while (PeekMessage(&msg, 0, 0, 0, PM_REMOVE)) {
            if (msg.message == WM_QUIT) running = false;
            TranslateMessage(&msg);
            DispatchMessage(&msg);
        }
        if (!running) break;

        OG_ImplOpenGL2_NewFrame();
        OG_ImplWin32_NewFrame();
        og::NewFrame();

        // ---- UI ----
        og::ShowDemoWindow(&show_demo);
        og::ShowNodeEditorDemo(&show_nodes);
        og::ShowThemeEditor(&show_theme_editor);
        og::RenderNotifications();

        // ---- render ----
        og::Render();
        og::U32 canvas = og::GetStyle().colors[og::Col_MenuBarBg];
        float clear_r = (float)(canvas & 255) / 255.0f;
        float clear_g = (float)((canvas >> 8) & 255) / 255.0f;
        float clear_b = (float)((canvas >> 16) & 255) / 255.0f;
        glClearColor(clear_r, clear_g, clear_b, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);
        OG_ImplOpenGL2_RenderDrawData(og::GetDrawData());
        SwapBuffers(g_hDC);
    }

    OG_ImplOpenGL2_Shutdown();
    OG_ImplWin32_Shutdown();
    og::DestroyContext();
    DestroyGLContext(hwnd);
    DestroyWindow(hwnd);
    UnregisterClassA("osGUIWindow", hInst);
    return 0;
}
