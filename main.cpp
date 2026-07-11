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
                              WS_OVERLAPPEDWINDOW, 100, 100, 1000, 760,
                              0, 0, hInst, 0);
    if (!CreateGLContext(hwnd)) { MessageBoxA(0, "Failed to create OpenGL context", "osGUI", MB_OK); return 1; }

    ShowWindow(hwnd, nCmdShow);
    UpdateWindow(hwnd);

    og::CreateContext();
    OG_ImplWin32_Init(hwnd);
    OG_ImplOpenGL2_Init();

    float clear_col[3] = { 0.035f, 0.039f, 0.065f };
    bool show_demo = true;

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

        // ---- render ----
        og::Render();
        glClearColor(clear_col[0], clear_col[1], clear_col[2], 1.0f);
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
