#include "osgui_impl_glfw.h"
#include <GLFW/glfw3.h>
#include <stdlib.h>
#include <string.h>

static GLFWwindow* g_Window = 0;
static double g_Time = 0.0;
static OG_GlfwFontBuilder g_FontBuilder = 0;
static void* g_FontUser = 0;
static float g_Dpi = 1.0f;
static bool g_InstalledCallbacks = false;
static unsigned char* g_OwnedFontPixels = 0;

static GLFWkeyfun g_PrevKey = 0;
static GLFWcharfun g_PrevChar = 0;
static GLFWscrollfun g_PrevScroll = 0;
static GLFWmousebuttonfun g_PrevMouseButton = 0;
static GLFWcursorposfun g_PrevCursorPos = 0;
static GLFWwindowfocusfun g_PrevWindowFocus = 0;

static og::Key OG_ImplGlfw_MapKey(int key) {
    if (key >= GLFW_KEY_A && key <= GLFW_KEY_Z)
        return (og::Key)((int)og::Key_A + key - GLFW_KEY_A);
    if (key >= GLFW_KEY_0 && key <= GLFW_KEY_9)
        return (og::Key)((int)og::Key_0 + key - GLFW_KEY_0);
    if (key >= GLFW_KEY_F1 && key <= GLFW_KEY_F12)
        return (og::Key)((int)og::Key_F1 + key - GLFW_KEY_F1);
    switch (key) {
    case GLFW_KEY_TAB: return og::Key_Tab;
    case GLFW_KEY_ENTER: case GLFW_KEY_KP_ENTER: return og::Key_Enter;
    case GLFW_KEY_ESCAPE: return og::Key_Escape;
    case GLFW_KEY_SPACE: return og::Key_Space;
    case GLFW_KEY_BACKSPACE: return og::Key_Backspace;
    case GLFW_KEY_DELETE: return og::Key_Delete;
    case GLFW_KEY_INSERT: return og::Key_Insert;
    case GLFW_KEY_LEFT: return og::Key_LeftArrow;
    case GLFW_KEY_UP: return og::Key_UpArrow;
    case GLFW_KEY_RIGHT: return og::Key_RightArrow;
    case GLFW_KEY_DOWN: return og::Key_DownArrow;
    case GLFW_KEY_PAGE_UP: return og::Key_PageUp;
    case GLFW_KEY_PAGE_DOWN: return og::Key_PageDown;
    case GLFW_KEY_HOME: return og::Key_Home;
    case GLFW_KEY_END: return og::Key_End;
    case GLFW_KEY_LEFT_SHIFT: case GLFW_KEY_RIGHT_SHIFT: return og::Key_Shift;
    case GLFW_KEY_LEFT_CONTROL: case GLFW_KEY_RIGHT_CONTROL: return og::Key_Ctrl;
    case GLFW_KEY_LEFT_ALT: case GLFW_KEY_RIGHT_ALT: return og::Key_Alt;
    default: return og::Key_None;
    }
}

static bool OG_ImplGlfw_ModifierStillDown(GLFWwindow* window, int key) {
    if (key == GLFW_KEY_LEFT_SHIFT) return glfwGetKey(window, GLFW_KEY_RIGHT_SHIFT) == GLFW_PRESS;
    if (key == GLFW_KEY_RIGHT_SHIFT) return glfwGetKey(window, GLFW_KEY_LEFT_SHIFT) == GLFW_PRESS;
    if (key == GLFW_KEY_LEFT_CONTROL) return glfwGetKey(window, GLFW_KEY_RIGHT_CONTROL) == GLFW_PRESS;
    if (key == GLFW_KEY_RIGHT_CONTROL) return glfwGetKey(window, GLFW_KEY_LEFT_CONTROL) == GLFW_PRESS;
    if (key == GLFW_KEY_LEFT_ALT) return glfwGetKey(window, GLFW_KEY_RIGHT_ALT) == GLFW_PRESS;
    if (key == GLFW_KEY_RIGHT_ALT) return glfwGetKey(window, GLFW_KEY_LEFT_ALT) == GLFW_PRESS;
    return false;
}

static void OG_ImplGlfw_KeyCallback(GLFWwindow* window, int key, int scancode, int action, int mods) {
    if (og::GetCurrentContext()) {
        og::Key mapped = OG_ImplGlfw_MapKey(key);
        if (mapped != og::Key_None) {
            bool down = action != GLFW_RELEASE;
            if (!down && OG_ImplGlfw_ModifierStillDown(window, key)) down = true;
            og::GetIO().AddKeyEvent(mapped, down);
        }
    }
    if (g_PrevKey) g_PrevKey(window, key, scancode, action, mods);
}

static void OG_ImplGlfw_CharCallback(GLFWwindow* window, unsigned int codepoint) {
    if (og::GetCurrentContext()) og::GetIO().AddInputCharacter(codepoint);
    if (g_PrevChar) g_PrevChar(window, codepoint);
}

static void OG_ImplGlfw_ScrollCallback(GLFWwindow* window, double x, double y) {
    if (og::GetCurrentContext()) og::GetIO().AddMouseWheelEvent((float)x, (float)y);
    if (g_PrevScroll) g_PrevScroll(window, x, y);
}

static void OG_ImplGlfw_MouseButtonCallback(GLFWwindow* window, int button, int action, int mods) {
    if (og::GetCurrentContext() && button >= 0 && button < 5)
        og::GetIO().AddMouseButtonEvent(button, action != GLFW_RELEASE);
    if (g_PrevMouseButton) g_PrevMouseButton(window, button, action, mods);
}

static void OG_ImplGlfw_CursorPosCallback(GLFWwindow* window, double x, double y) {
    if (og::GetCurrentContext()) og::GetIO().AddMousePosEvent((float)x, (float)y);
    if (g_PrevCursorPos) g_PrevCursorPos(window, x, y);
}

static void OG_ImplGlfw_WindowFocusCallback(GLFWwindow* window, int focused) {
    if (og::GetCurrentContext()) og::GetIO().AddFocusEvent(focused != 0);
    if (g_PrevWindowFocus) g_PrevWindowFocus(window, focused);
}

static const char* OG_ImplGlfw_GetClipboard(void*) {
    if (!g_Window) return "";
    const char* text = glfwGetClipboardString(g_Window);
    return text ? text : "";
}

static void OG_ImplGlfw_SetClipboard(void*, const char* text) {
    if (g_Window) glfwSetClipboardString(g_Window, text ? text : "");
}

void OG_ImplGlfw_SetFontBuilder(OG_GlfwFontBuilder builder, void* user) {
    g_FontBuilder = builder;
    g_FontUser = user;
}

bool OG_ImplGlfw_Init(GLFWwindow* window, bool install_callbacks) {
    if (!window || g_Window || !og::GetCurrentContext()) return false;
    g_Window = window;
    g_Time = glfwGetTime();

    og::IO& io = og::GetIO();
    io.get_clipboard_text = OG_ImplGlfw_GetClipboard;
    io.set_clipboard_text = OG_ImplGlfw_SetClipboard;
    io.clipboard_user_data = 0;
    io.backend_platform_name = "osgui_impl_glfw";
    io.backend_flags |= og::BackendFlags_HasClipboard;
    io.app_focused = glfwGetWindowAttrib(window, GLFW_FOCUSED) != 0;

    if (install_callbacks) {
        g_PrevKey = glfwSetKeyCallback(window, OG_ImplGlfw_KeyCallback);
        g_PrevChar = glfwSetCharCallback(window, OG_ImplGlfw_CharCallback);
        g_PrevScroll = glfwSetScrollCallback(window, OG_ImplGlfw_ScrollCallback);
        g_PrevMouseButton = glfwSetMouseButtonCallback(window, OG_ImplGlfw_MouseButtonCallback);
        g_PrevCursorPos = glfwSetCursorPosCallback(window, OG_ImplGlfw_CursorPosCallback);
        g_PrevWindowFocus = glfwSetWindowFocusCallback(window, OG_ImplGlfw_WindowFocusCallback);
        g_InstalledCallbacks = true;
    }
    return true;
}

void OG_ImplGlfw_Shutdown() {
    if (g_Window && g_InstalledCallbacks) {
        // Restore even null callbacks. Conditional restoration left dangling
        // OSGui callbacks whenever the application originally had no handler.
        glfwSetKeyCallback(g_Window, g_PrevKey);
        glfwSetCharCallback(g_Window, g_PrevChar);
        glfwSetScrollCallback(g_Window, g_PrevScroll);
        glfwSetMouseButtonCallback(g_Window, g_PrevMouseButton);
        glfwSetCursorPosCallback(g_Window, g_PrevCursorPos);
        glfwSetWindowFocusCallback(g_Window, g_PrevWindowFocus);
    }
    unsigned char* owned_pixels = g_OwnedFontPixels;
    if (og::GetCurrentContext()) {
        og::IO& io = og::GetIO();
        io.AddFocusEvent(false);
        og::FontAtlas& atlas = og::GetFontAtlas();
        if (owned_pixels && atlas.pixels == owned_pixels) {
            atlas.pixels = 0;
            atlas.width = atlas.height = 0;
            atlas.line_height = atlas.ascent = 0.0f;
            atlas.white_uv = og::Vec2();
            atlas.tex_id = 0;
            atlas.glyph_map.clear();
            for (int i = 0; i < 128; ++i) atlas.glyph_valid[i] = false;
        }
        if (io.get_clipboard_text == OG_ImplGlfw_GetClipboard) io.get_clipboard_text = 0;
        if (io.set_clipboard_text == OG_ImplGlfw_SetClipboard) io.set_clipboard_text = 0;
        if (io.backend_platform_name && strcmp(io.backend_platform_name, "osgui_impl_glfw") == 0)
            io.backend_platform_name = 0;
        io.backend_flags &= ~og::BackendFlags_HasClipboard;
    }
    if (owned_pixels) free(owned_pixels);
    g_OwnedFontPixels = 0;
    g_Window = 0;
    g_Time = 0.0;
    g_Dpi = 1.0f;
    g_InstalledCallbacks = false;
    g_PrevKey = 0; g_PrevChar = 0; g_PrevScroll = 0;
    g_PrevMouseButton = 0; g_PrevCursorPos = 0; g_PrevWindowFocus = 0;
}

static bool OG_ImplGlfw_RebuildFont(float dpi) {
    if (!g_FontBuilder || !og::GetCurrentContext()) return false;
    og::FontAtlas candidate;
    candidate.pixels = 0;
    candidate.width = candidate.height = 0;
    candidate.line_height = candidate.ascent = 0.0f;
    candidate.tex_id = 0;
    for (int i = 0; i < 128; ++i) candidate.glyph_valid[i] = false;

    bool built = g_FontBuilder(candidate, dpi, g_FontUser);
    if (!built || !candidate.pixels || candidate.width <= 0 || candidate.height <= 0 || candidate.line_height <= 0.0f) {
        if (candidate.pixels && candidate.pixels != g_OwnedFontPixels) free(candidate.pixels);
        return false;
    }

    og::FontAtlas& atlas = og::GetFontAtlas();
    if (g_OwnedFontPixels && g_OwnedFontPixels != candidate.pixels) free(g_OwnedFontPixels);
    g_OwnedFontPixels = candidate.pixels;
    atlas = candidate;
    atlas.tex_id = 0;
    return true;
}

void OG_ImplGlfw_NewFrame() {
    if (!g_Window || !og::GetCurrentContext()) return;
    og::IO& io = og::GetIO();

    const bool focused = glfwGetWindowAttrib(g_Window, GLFW_FOCUSED) != 0;
    if (io.app_focused != focused) io.AddFocusEvent(focused);

    int width = 0, height = 0, framebuffer_width = 0, framebuffer_height = 0;
    glfwGetWindowSize(g_Window, &width, &height);
    glfwGetFramebufferSize(g_Window, &framebuffer_width, &framebuffer_height);
    io.display_size = og::Vec2((float)width, (float)height);
    io.framebuffer_scale = og::Vec2(width > 0 ? (float)framebuffer_width / width : 1.0f,
                                    height > 0 ? (float)framebuffer_height / height : 1.0f);

    float x_scale = 1.0f, y_scale = 1.0f;
    glfwGetWindowContentScale(g_Window, &x_scale, &y_scale);
    float dpi = x_scale > 0.0f ? x_scale : 1.0f;
    if (dpi < 0.5f) dpi = 0.5f;
    if (dpi > 3.0f) dpi = 3.0f;
    if (dpi != g_Dpi || !og::GetFontAtlas().pixels) {
        const bool needs_font = !og::GetFontAtlas().pixels;
        const bool font_ready = g_FontBuilder ? OG_ImplGlfw_RebuildFont(dpi) : !needs_font;
        if (font_ready) {
            g_Dpi = dpi;
            io.dpi_scale = dpi;
            og::SetUIScale(dpi);
        }
    }

    double now = glfwGetTime();
    io.delta_time = g_Time > 0.0 ? (float)(now - g_Time) : 1.0f / 60.0f;
    if (io.delta_time <= 0.0f) io.delta_time = 1.0f / 60.0f;
    g_Time = now;

    // Polling complements callbacks when callbacks are not installed and also
    // repairs state after event loss while a window is being dragged.
    double mouse_x = 0.0, mouse_y = 0.0;
    glfwGetCursorPos(g_Window, &mouse_x, &mouse_y);
    if (io.mouse_pos.x != (float)mouse_x || io.mouse_pos.y != (float)mouse_y)
        io.AddMousePosEvent((float)mouse_x, (float)mouse_y);
    for (int button = 0; button < 5; ++button) {
        bool down = glfwGetMouseButton(g_Window, button) == GLFW_PRESS;
        if (io.mouse_down[button] != down) io.AddMouseButtonEvent(button, down);
    }
}
