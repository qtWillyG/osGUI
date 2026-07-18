// OSGui v2 showcase host: Win32 + dependency-free compatibility OpenGL.
#include "osgui.h"
#include "osgui_impl_opengl2.h"
#include "osgui_impl_win32.h"

#include <GL/gl.h>
#include <windows.h>
#include <string.h>

static HGLRC g_gl_context = 0;
static HDC g_device_context = 0;

static void ShowStartupError(const char* message) {
    MessageBoxA(0, message, "OSGui v2 startup error", MB_OK | MB_ICONERROR);
}

static bool CreateGLContext(HWND window) {
    g_device_context = GetDC(window);
    if (!g_device_context)
        return false;

    PIXELFORMATDESCRIPTOR descriptor;
    memset(&descriptor, 0, sizeof(descriptor));
    descriptor.nSize = (WORD)sizeof(descriptor);
    descriptor.nVersion = 1;
    descriptor.dwFlags = PFD_DRAW_TO_WINDOW | PFD_SUPPORT_OPENGL | PFD_DOUBLEBUFFER;
    descriptor.iPixelType = PFD_TYPE_RGBA;
    descriptor.cColorBits = 32;
    descriptor.cDepthBits = 24;
    descriptor.iLayerType = PFD_MAIN_PLANE;

    const int pixel_format = ChoosePixelFormat(g_device_context, &descriptor);
    if (!pixel_format || !SetPixelFormat(g_device_context, pixel_format, &descriptor)) {
        ReleaseDC(window, g_device_context);
        g_device_context = 0;
        return false;
    }

    g_gl_context = wglCreateContext(g_device_context);
    if (!g_gl_context || !wglMakeCurrent(g_device_context, g_gl_context)) {
        if (g_gl_context)
            wglDeleteContext(g_gl_context);
        ReleaseDC(window, g_device_context);
        g_gl_context = 0;
        g_device_context = 0;
        return false;
    }
    return true;
}

static void DestroyGLContext(HWND window) {
    wglMakeCurrent(0, 0);
    if (g_gl_context)
        wglDeleteContext(g_gl_context);
    if (g_device_context)
        ReleaseDC(window, g_device_context);
    g_gl_context = 0;
    g_device_context = 0;
}

static LRESULT CALLBACK WndProc(HWND window, UINT message, WPARAM w_param, LPARAM l_param) {
    if (OG_ImplWin32_WndProcHandler(window, message, w_param, l_param))
        return 1;

    switch (message) {
    case WM_SIZE:
        if (g_gl_context)
            glViewport(0, 0, LOWORD(l_param), HIWORD(l_param));
        return 0;
    case WM_CLOSE:
        PostQuitMessage(0);
        return 0;
    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
    default:
        return DefWindowProc(window, message, w_param, l_param);
    }
}

int WINAPI WinMain(HINSTANCE instance, HINSTANCE, LPSTR, int show_command) {
    // Ask Windows for crisp physical-pixel rendering while retaining a fallback
    // for systems that predate per-monitor-v2 DPI awareness.
    typedef BOOL(WINAPI* SetDpiContextFn)(HANDLE);
    const FARPROC dpi_proc = GetProcAddress(
        GetModuleHandleA("user32.dll"), "SetProcessDpiAwarenessContext");
    SetDpiContextFn set_dpi_context = 0;
    static_assert(sizeof(set_dpi_context) == sizeof(dpi_proc),
                  "Windows procedure pointers must have a uniform representation");
    memcpy(&set_dpi_context, &dpi_proc, sizeof(set_dpi_context));

    if (set_dpi_context) {
        const INT_PTR per_monitor_v2_value = -4;
        HANDLE per_monitor_v2 = 0;
        static_assert(sizeof(per_monitor_v2) == sizeof(per_monitor_v2_value),
                      "A DPI awareness context must be pointer-width");
        memcpy(&per_monitor_v2, &per_monitor_v2_value, sizeof(per_monitor_v2));
        set_dpi_context(per_monitor_v2);
    } else {
        SetProcessDPIAware();
    }

    WNDCLASSEXA window_class;
    memset(&window_class, 0, sizeof(window_class));
    window_class.cbSize = sizeof(window_class);
    window_class.style = CS_HREDRAW | CS_VREDRAW | CS_OWNDC;
    window_class.lpfnWndProc = WndProc;
    window_class.hInstance = instance;
    window_class.hCursor = LoadCursor(0, IDC_ARROW);
    window_class.lpszClassName = "OSGuiV2Showcase";
    if (!RegisterClassExA(&window_class)) {
        ShowStartupError("Failed to register the showcase window class.");
        return 1;
    }

    HWND window = CreateWindowA(
        window_class.lpszClassName,
        "OSGui 2.0 Alpha - Native Instrument UI",
        WS_OVERLAPPEDWINDOW,
        30,
        30,
        1480,
        940,
        0,
        0,
        instance,
        0);
    if (!window) {
        ShowStartupError("Failed to create the showcase window.");
        UnregisterClassA(window_class.lpszClassName, instance);
        return 1;
    }

    if (!CreateGLContext(window)) {
        ShowStartupError("Failed to create the compatibility OpenGL context.");
        DestroyWindow(window);
        UnregisterClassA(window_class.lpszClassName, instance);
        return 1;
    }

    og::Context* context = og::CreateContext();
    if (!context || !OG_CHECKVERSION()) {
        ShowStartupError("OSGui headers and the linked core have incompatible layouts.");
        og::DestroyContext(context);
        DestroyGLContext(window);
        DestroyWindow(window);
        UnregisterClassA(window_class.lpszClassName, instance);
        return 1;
    }

    if (!OG_ImplWin32_Init(window)) {
        ShowStartupError("The Win32 platform backend could not be initialized.");
        og::DestroyContext(context);
        DestroyGLContext(window);
        DestroyWindow(window);
        UnregisterClassA(window_class.lpszClassName, instance);
        return 1;
    }

    if (!OG_ImplOpenGL2_Init()) {
        ShowStartupError("The compatibility OpenGL renderer could not be initialized.");
        OG_ImplWin32_Shutdown();
        og::DestroyContext(context);
        DestroyGLContext(window);
        DestroyWindow(window);
        UnregisterClassA(window_class.lpszClassName, instance);
        return 1;
    }

    ShowWindow(window, show_command);
    UpdateWindow(window);

    bool show_demo = true;
    bool show_nodes = true;
    bool show_theme_editor = true;
    bool show_metrics = true;
    bool running = true;

    while (running) {
        MSG message;
        while (PeekMessage(&message, 0, 0, 0, PM_REMOVE)) {
            if (message.message == WM_QUIT)
                running = false;
            TranslateMessage(&message);
            DispatchMessage(&message);
        }
        if (!running)
            break;

        OG_ImplWin32_NewFrame();
        OG_ImplOpenGL2_NewFrame();
        og::NewFrame();

        og::ShowDemoWindow(&show_demo);
        og::ShowNodeEditorDemo(&show_nodes);
        og::ShowThemeEditor(&show_theme_editor);
        og::ShowMetricsWindow(&show_metrics);
        og::RenderNotifications();

        og::Render();
        const og::U32 canvas = og::GetStyle().colors[og::Col_MenuBarBg];
        const float clear_r = (float)(canvas & 255) / 255.0f;
        const float clear_g = (float)((canvas >> 8) & 255) / 255.0f;
        const float clear_b = (float)((canvas >> 16) & 255) / 255.0f;
        glClearColor(clear_r, clear_g, clear_b, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);
        OG_ImplOpenGL2_RenderDrawData(og::GetDrawData());
        SwapBuffers(g_device_context);
    }

    OG_ImplOpenGL2_Shutdown();
    OG_ImplWin32_Shutdown();
    og::DestroyContext(context);
    DestroyGLContext(window);
    DestroyWindow(window);
    UnregisterClassA(window_class.lpszClassName, instance);
    return 0;
}
