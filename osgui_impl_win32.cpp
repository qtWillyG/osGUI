// OSGui Win32 platform backend.
// Responsibilities:
//   * feed mouse / wheel / keyboard / text input into og::IO
//   * provide display size and per-frame delta time
//   * bake the font atlas (here via GDI, since the core has no TrueType baker)
#include "osgui_impl_win32.h"
#include "osgui.h"
#include <windows.h>
#include <string>
#include <vector>

static HWND          g_hWnd = 0;
static INT64         g_Time = 0;
static INT64         g_TicksPerSecond = 0;
static char          g_FontFamily[LF_FACESIZE] = "Segoe UI";
static char          g_FontFallback[LF_FACESIZE] = "Microsoft YaHei UI";
static char          g_FontEmoji[LF_FACESIZE] = "Segoe UI Emoji";
static int           g_FontPixels = 17;
static int           g_FontWeight = FW_NORMAL;
static WCHAR         g_PendingHighSurrogate = 0;
static float         g_DpiScale = 1.0f;
static std::string   g_ClipboardUTF8;

static int OG_ImplWin32_EncodeUTF16(unsigned int cp, WCHAR out[2]) {
    if (cp <= 0xFFFF) { out[0] = (WCHAR)cp; return 1; }
    cp -= 0x10000; out[0] = (WCHAR)(0xD800 + (cp >> 10)); out[1] = (WCHAR)(0xDC00 + (cp & 0x3FF)); return 2;
}

static float OG_ImplWin32_QueryDpiScale() {
    typedef UINT (WINAPI* GetDpiForWindowFn)(HWND);
    HMODULE user32 = GetModuleHandleA("user32.dll");
    GetDpiForWindowFn get_dpi = user32 ? (GetDpiForWindowFn)GetProcAddress(user32, "GetDpiForWindow") : 0;
    UINT dpi = get_dpi && g_hWnd ? get_dpi(g_hWnd) : 0;
    if (!dpi) {
        HDC dc = GetDC(g_hWnd);
        dpi = dc ? (UINT)GetDeviceCaps(dc, LOGPIXELSX) : 96;
        if (dc) ReleaseDC(g_hWnd, dc);
    }
    return (float)dpi / 96.0f;
}

static const char* OG_ImplWin32_GetClipboardText(void*) {
    g_ClipboardUTF8.clear();
    if (!OpenClipboard(g_hWnd)) return g_ClipboardUTF8.c_str();
    HANDLE data = GetClipboardData(CF_UNICODETEXT);
    const WCHAR* wide = data ? (const WCHAR*)GlobalLock(data) : 0;
    if (wide) {
        int count = WideCharToMultiByte(CP_UTF8, 0, wide, -1, 0, 0, 0, 0);
        if (count > 1) {
            g_ClipboardUTF8.resize((size_t)count);
            WideCharToMultiByte(CP_UTF8, 0, wide, -1, &g_ClipboardUTF8[0], count, 0, 0);
            g_ClipboardUTF8.resize((size_t)count - 1);
        }
        GlobalUnlock(data);
    }
    CloseClipboard();
    return g_ClipboardUTF8.c_str();
}

static void OG_ImplWin32_SetClipboardText(void*, const char* text) {
    if (!text || !OpenClipboard(g_hWnd)) return;
    int count = MultiByteToWideChar(CP_UTF8, 0, text, -1, 0, 0);
    HGLOBAL data = count > 0 ? GlobalAlloc(GMEM_MOVEABLE, (size_t)count * sizeof(WCHAR)) : 0;
    WCHAR* wide = data ? (WCHAR*)GlobalLock(data) : 0;
    if (wide) {
        MultiByteToWideChar(CP_UTF8, 0, text, -1, wide, count);
        GlobalUnlock(data);
        EmptyClipboard();
        if (!SetClipboardData(CF_UNICODETEXT, data)) GlobalFree(data);
    } else if (data) GlobalFree(data);
    CloseClipboard();
}

// ---------------------------------------------------------------------
//  Font baking via GDI -> fills og::FontAtlas (R8 coverage)
// ---------------------------------------------------------------------
static void OG_ImplWin32_BuildFont() {
    og::FontAtlas& atlas = og::GetFontAtlas();

    HDC screen = GetDC(0);
    HDC dc = CreateCompatibleDC(screen);

    int scaled_pixels = (int)(g_FontPixels * g_DpiScale + 0.5f);
    HFONT font = CreateFontA(-scaled_pixels, 0, 0, 0, g_FontWeight, FALSE, FALSE, FALSE,
                             ANSI_CHARSET, OUT_TT_PRECIS, CLIP_DEFAULT_PRECIS,
                             CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, g_FontFamily);
    if (!font) font = (HFONT)GetStockObject(ANSI_FIXED_FONT);
    HFONT fallback = CreateFontA(-scaled_pixels, 0, 0, 0, g_FontWeight, FALSE, FALSE, FALSE,
                                 DEFAULT_CHARSET, OUT_TT_PRECIS, CLIP_DEFAULT_PRECIS,
                                 CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, g_FontFallback);
    if (!fallback) fallback = font;
    HFONT emoji = CreateFontA(-scaled_pixels, 0, 0, 0, g_FontWeight, FALSE, FALSE, FALSE,
                              DEFAULT_CHARSET, OUT_TT_PRECIS, CLIP_DEFAULT_PRECIS,
                              CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, g_FontEmoji);
    if (!emoji) emoji = fallback;
    HGDIOBJ old_font = SelectObject(dc, font);

    TEXTMETRICA tm;
    GetTextMetricsA(dc, &tm);
    std::vector<unsigned int> codepoints;
    for (unsigned int cp = 32; cp <= 126; ++cp) codepoints.push_back(cp);
    for (unsigned int cp = 0x00A0; cp <= 0x024F; ++cp) codepoints.push_back(cp);
    for (unsigned int cp = 0x0370; cp <= 0x052F; ++cp) codepoints.push_back(cp);
    for (unsigned int cp = 0x0590; cp <= 0x06FF; ++cp) codepoints.push_back(cp);
    for (unsigned int cp = 0x0900; cp <= 0x097F; ++cp) codepoints.push_back(cp);
    for (unsigned int cp = 0x2190; cp <= 0x21FF; ++cp) codepoints.push_back(cp);
    for (unsigned int cp = 0x2500; cp <= 0x257F; ++cp) codepoints.push_back(cp);
    for (unsigned int cp = 0x2600; cp <= 0x27BF; ++cp) codepoints.push_back(cp);
    for (unsigned int cp = 0x3040; cp <= 0x30FF; ++cp) codepoints.push_back(cp);
    for (unsigned int cp = 0x4E00; cp <= 0x9FFF; ++cp) codepoints.push_back(cp);
    for (unsigned int cp = 0x1F300; cp <= 0x1F64F; ++cp) codepoints.push_back(cp);

    std::vector<int> glyph_width(codepoints.size(), tm.tmAveCharWidth);
    std::vector<int> font_choice(codepoints.size(), 0);
    int cellW = tm.tmAveCharWidth;
    for (size_t i = 0; i < codepoints.size(); ++i) {
        WCHAR ch[2]; int char_count = OG_ImplWin32_EncodeUTF16(codepoints[i], ch);
        WORD glyph_index[2] = { 0, 0 };
        SelectObject(dc, font);
        if (GetGlyphIndicesW(dc, ch, char_count, glyph_index, GGI_MARK_NONEXISTING_GLYPHS) == GDI_ERROR || glyph_index[0] == 0xFFFF) {
            font_choice[i] = 1;
            SelectObject(dc, fallback);
            glyph_index[0] = glyph_index[1] = 0;
            if (GetGlyphIndicesW(dc, ch, char_count, glyph_index, GGI_MARK_NONEXISTING_GLYPHS) == GDI_ERROR || glyph_index[0] == 0xFFFF) {
                font_choice[i] = 2;
                SelectObject(dc, emoji);
            }
        }
        SIZE extent = { 0, 0 };
        if (GetTextExtentPoint32W(dc, ch, char_count, &extent)) glyph_width[i] = extent.cx;
        if (glyph_width[i] < 1) glyph_width[i] = tm.tmAveCharWidth;
        if (glyph_width[i] + 3 > cellW) cellW = glyph_width[i] + 3;
    }
    int cellH = tm.tmHeight;
    if (cellW < 1) cellW = 8;
    if (cellH < 1) cellH = 13;

    const int cols = 64;
    const int rows = ((int)codepoints.size() + cols - 1) / cols;
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
    for (size_t i = 0; i < codepoints.size(); ++i) {
        SelectObject(dc, font_choice[i] == 0 ? font : (font_choice[i] == 1 ? fallback : emoji));
        int ci = (int)i;
        int cx = (ci % cols) * cellW;
        int cy = (ci / cols) * cellH;
        WCHAR ch[2]; int char_count = OG_ImplWin32_EncodeUTF16(codepoints[i], ch);
        TextOutW(dc, cx + 1, cy, ch, char_count);
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

    if (atlas.pixels) free(atlas.pixels);
    atlas.pixels = pixels;
    atlas.width = W; atlas.height = H;
    atlas.line_height = (float)cellH;
    atlas.ascent = (float)tm.tmAscent;
    atlas.white_uv = og::Vec2((wx + 0.5f) / W, (wy + 0.5f) / H);
    atlas.tex_id = 0;

    atlas.glyph_map.clear();
    for (int i = 0; i < 128; i++) atlas.glyph_valid[i] = false;
    for (size_t i = 0; i < codepoints.size(); ++i) {
        unsigned int cp = codepoints[i];
        int ci = (int)i;
        int cx = (ci % cols) * cellW;
        int cy = (ci / cols) * cellH;
        og::Glyph glyph;
        glyph.advance = (float)glyph_width[i];
        glyph.x0 = 0; glyph.y0 = 0; glyph.x1 = (float)cellW; glyph.y1 = (float)cellH;
        glyph.u0 = (float)cx / W;            glyph.v0 = (float)cy / H;
        glyph.u1 = (float)(cx + cellW) / W;  glyph.v1 = (float)(cy + cellH) / H;
        atlas.glyph_map[cp] = glyph;
        if (cp < 128) { atlas.glyphs[cp] = glyph; atlas.glyph_valid[cp] = true; }
    }

    SelectObject(dc, old_bmp);
    SelectObject(dc, old_font);
    DeleteObject(dib);
    if (emoji != fallback && emoji != font) DeleteObject(emoji);
    if (fallback != font) DeleteObject(fallback);
    DeleteObject(font);
    DeleteDC(dc);
    ReleaseDC(0, screen);
}

bool OG_ImplWin32_SetFont(const char* family, int pixel_height, int weight) {
    if (!family || !family[0] || pixel_height < 9 || pixel_height > 48) return false;
    strncpy_s(g_FontFamily, sizeof(g_FontFamily), family, _TRUNCATE);
    g_FontPixels = pixel_height;
    g_FontWeight = weight;
    OG_ImplWin32_BuildFont();
    return true;
}
bool OG_ImplWin32_SetFontFallback(const char* family) {
    if (!family || !family[0]) return false;
    strncpy_s(g_FontFallback, sizeof(g_FontFallback), family, _TRUNCATE);
    OG_ImplWin32_BuildFont();
    return true;
}
const char* OG_ImplWin32_GetFontFamily() { return g_FontFamily; }
const char* OG_ImplWin32_GetFontFallback() { return g_FontFallback; }
int OG_ImplWin32_GetFontSize() { return g_FontPixels; }

bool OG_ImplWin32_Init(void* hwnd) {
    g_hWnd = (HWND)hwnd;
    if (!QueryPerformanceFrequency((LARGE_INTEGER*)&g_TicksPerSecond)) return false;
    if (!QueryPerformanceCounter((LARGE_INTEGER*)&g_Time)) return false;
    g_DpiScale = OG_ImplWin32_QueryDpiScale();
    og::SetUIScale(g_DpiScale);
    og::IO& io = og::GetIO();
    io.dpi_scale = g_DpiScale;
    io.framebuffer_scale = og::Vec2(1.0f, 1.0f);
    io.get_clipboard_text = OG_ImplWin32_GetClipboardText;
    io.set_clipboard_text = OG_ImplWin32_SetClipboardText;
    io.clipboard_user_data = 0;
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

    float dpi_scale = OG_ImplWin32_QueryDpiScale();
    if (dpi_scale < 0.5f) dpi_scale = 0.5f;
    if (dpi_scale > 4.0f) dpi_scale = 4.0f;
    if (dpi_scale != g_DpiScale) {
        g_DpiScale = dpi_scale;
        io.dpi_scale = dpi_scale;
        og::SetUIScale(dpi_scale);
        OG_ImplWin32_BuildFont();
    }

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
        if (wParam >= 0xD800 && wParam <= 0xDBFF) { g_PendingHighSurrogate = (WCHAR)wParam; return 1; }
        if (io.input_char_count < 32 && wParam > 0) {
            unsigned int cp = (unsigned int)wParam;
            if (g_PendingHighSurrogate && wParam >= 0xDC00 && wParam <= 0xDFFF)
                cp = 0x10000 + (((unsigned int)g_PendingHighSurrogate - 0xD800) << 10) + ((unsigned int)wParam - 0xDC00);
            g_PendingHighSurrogate = 0;
            io.input_chars[io.input_char_count++] = cp;
        }
        return 1;
    }
    return 0;
}
