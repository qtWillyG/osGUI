// osgui_impl_win32.cpp - platform backend (cf. imgui_impl_win32.cpp)
//
// Responsibilities, mirroring the real backend:
//   * feed mouse / wheel / keyboard / text input into og::IO
//   * provide display size and per-frame delta time
//   * bake the font atlas (here via GDI, since the core has no TrueType baker)
#include "osgui_impl_win32.h"
#include "osgui.h"
#include <windows.h>

static HWND          g_hWnd = 0;
static INT64         g_Time = 0;
static INT64         g_TicksPerSecond = 0;

// ---------------------------------------------------------------------
//  Font baking via GDI -> fills og::FontAtlas (R8 coverage)
// ---------------------------------------------------------------------
static void OG_ImplWin32_BuildFont() {
    og::FontAtlas& atlas = og::GetFontAtlas();

    HDC screen = GetDC(0);
    HDC dc = CreateCompatibleDC(screen);

    int px = 15; // target pixel height
    HFONT font = CreateFontA(-px, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                             ANSI_CHARSET, OUT_TT_PRECIS, CLIP_DEFAULT_PRECIS,
                             CLEARTYPE_QUALITY, FIXED_PITCH | FF_MODERN, "Consolas");
    if (!font) font = (HFONT)GetStockObject(ANSI_FIXED_FONT);
    HGDIOBJ old_font = SelectObject(dc, font);

    TEXTMETRICA tm;
    GetTextMetricsA(dc, &tm);
    int cellW = tm.tmAveCharWidth;
    int cellH = tm.tmHeight;
    if (cellW < 1) cellW = 8;
    if (cellH < 1) cellH = 13;

    const int cols = 16, rows = 6;            // 96 cells for chars 32..127
    int glyph_h = rows * cellH;
    int W = cols * cellW;
    int H = glyph_h + 2;                       // +2 rows reserved for the white texel

    // 32-bit top-down DIB to render white glyphs on black
    BITMAPINFO bmi; memset(&bmi, 0, sizeof(bmi));
    bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bmi.bmiHeader.biWidth = W;
    bmi.bmiHeader.biHeight = -H;               // negative => top-down
    bmi.bmiHeader.biPlanes = 1;
    bmi.bmiHeader.biBitCount = 32;
    bmi.bmiHeader.biCompression = BI_RGB;
    void* bits = 0;
    HBITMAP dib = CreateDIBSection(dc, &bmi, DIB_RGB_COLORS, &bits, 0, 0);
    HGDIOBJ old_bmp = SelectObject(dc, dib);

    RECT full = { 0, 0, W, H };
    HBRUSH black = (HBRUSH)GetStockObject(BLACK_BRUSH);
    FillRect(dc, &full, black);
    SetBkMode(dc, TRANSPARENT);
    SetTextColor(dc, RGB(255, 255, 255));
    SetTextAlign(dc, TA_TOP | TA_LEFT);
    SelectObject(dc, font);

    for (int c = 32; c < 128; c++) {
        int ci = c - 32;
        int cx = (ci % cols) * cellW;
        int cy = (ci / cols) * cellH;
        char ch = (char)c;
        TextOutA(dc, cx, cy, &ch, 1);
    }
    GdiFlush();

    // allocate atlas (R8) and copy coverage from the green channel
    unsigned char* pixels = (unsigned char*)malloc((size_t)W * H);
    memset(pixels, 0, (size_t)W * H);
    unsigned char* src = (unsigned char*)bits; // BGRA
    for (int y = 0; y < glyph_h; y++)
        for (int x = 0; x < W; x++)
            pixels[y * W + x] = src[(y * W + x) * 4 + 1]; // green == coverage

    // reserved fully-white texel at (0, glyph_h)
    int wx = 0, wy = glyph_h;
    pixels[wy * W + wx] = 255;

    atlas.pixels = pixels;
    atlas.width = W; atlas.height = H;
    atlas.line_height = (float)cellH;
    atlas.ascent = (float)tm.tmAscent;
    atlas.white_uv = og::Vec2((wx + 0.5f) / W, (wy + 0.5f) / H);
    atlas.tex_id = 0;

    for (int i = 0; i < 128; i++) atlas.glyph_valid[i] = false;
    for (int c = 32; c < 128; c++) {
        int ci = c - 32;
        int cx = (ci % cols) * cellW;
        int cy = (ci / cols) * cellH;
        og::Glyph& g = atlas.glyphs[c];
        g.advance = (float)cellW;
        g.x0 = 0; g.y0 = 0; g.x1 = (float)cellW; g.y1 = (float)cellH;
        g.u0 = (float)cx / W;            g.v0 = (float)cy / H;
        g.u1 = (float)(cx + cellW) / W;  g.v1 = (float)(cy + cellH) / H;
        atlas.glyph_valid[c] = true;
    }

    SelectObject(dc, old_bmp);
    SelectObject(dc, old_font);
    DeleteObject(dib);
    DeleteObject(font);
    DeleteDC(dc);
    ReleaseDC(0, screen);
}

bool OG_ImplWin32_Init(void* hwnd) {
    g_hWnd = (HWND)hwnd;
    if (!QueryPerformanceFrequency((LARGE_INTEGER*)&g_TicksPerSecond)) return false;
    if (!QueryPerformanceCounter((LARGE_INTEGER*)&g_Time)) return false;
    OG_ImplWin32_BuildFont();
    return true;
}

void OG_ImplWin32_Shutdown() {
    og::FontAtlas& atlas = og::GetFontAtlas();
    if (atlas.pixels) { free(atlas.pixels); atlas.pixels = 0; }
    g_hWnd = 0;
}

void OG_ImplWin32_NewFrame() {
    og::IO& io = og::GetIO();

    RECT rect; GetClientRect(g_hWnd, &rect);
    io.display_size = og::Vec2((float)(rect.right - rect.left), (float)(rect.bottom - rect.top));

    INT64 now; QueryPerformanceCounter((LARGE_INTEGER*)&now);
    io.delta_time = (float)((double)(now - g_Time) / g_TicksPerSecond);
    if (io.delta_time <= 0) io.delta_time = 1.0f / 60.0f;
    g_Time = now;

    // mouse position (in case messages were missed, e.g. while dragging)
    POINT p;
    if (GetCursorPos(&p) && ScreenToClient(g_hWnd, &p))
        io.mouse_pos = og::Vec2((float)p.x, (float)p.y);
}

LRESULT OG_ImplWin32_WndProcHandler(HWND, UINT msg, WPARAM wParam, LPARAM lParam) {
    og::IO& io = og::GetIO();
    switch (msg) {
    case WM_LBUTTONDOWN: io.mouse_down[0] = true;  SetCapture(g_hWnd); return 1;
    case WM_RBUTTONDOWN: io.mouse_down[1] = true;  return 1;
    case WM_MBUTTONDOWN: io.mouse_down[2] = true;  return 1;
    case WM_LBUTTONUP:   io.mouse_down[0] = false; ReleaseCapture(); return 1;
    case WM_RBUTTONUP:   io.mouse_down[1] = false; return 1;
    case WM_MBUTTONUP:   io.mouse_down[2] = false; return 1;
    case WM_MOUSEMOVE:
        io.mouse_pos = og::Vec2((float)(short)LOWORD(lParam), (float)(short)HIWORD(lParam));
        return 1;
    case WM_MOUSEWHEEL:
        io.mouse_wheel += (float)GET_WHEEL_DELTA_WPARAM(wParam) / (float)WHEEL_DELTA;
        return 1;
    case WM_KEYDOWN:
    case WM_SYSKEYDOWN:
        if (wParam < 256) io.key_down[wParam] = true;
        return 1;
    case WM_KEYUP:
    case WM_SYSKEYUP:
        if (wParam < 256) io.key_down[wParam] = false;
        return 1;
    case WM_CHAR:
        if (io.input_char_count < 32 && wParam > 0 && wParam < 0x10000)
            io.input_chars[io.input_char_count++] = (unsigned int)wParam;
        return 1;
    }
    return 0;
}
