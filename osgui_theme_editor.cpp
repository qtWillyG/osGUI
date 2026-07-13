// Runtime theme, font, and shader-effects editor for the OSGui demo.
#include "osgui.h"
#include "osgui_impl_win32.h"
#include "osgui_impl_opengl2.h"

namespace og {

static float Channel(U32 color, int shift) { return (float)((color >> shift) & 255) / 255.0f; }

void ShowThemeEditor(bool* p_open) {
    SetNextWindowPos(Vec2(920, 430));
    SetNextWindowSize(Vec2(485, 420));
    if (!Begin("OSGui / Theme Studio", p_open)) { End(); return; }

    TextColored(Vec4(0.55f, 0.93f, 0.85f, 1.0f), "LIVE DESIGN TOKENS");
    TextDisabled("Every change is applied to the running interface.");

    if (Button("Midnight", Vec2(102, 32))) SetTheme(Theme_Dark, 0.3f);
    SameLine();
    if (Button("Daylight", Vec2(102, 32))) SetTheme(Theme_Light, 0.3f);

    Separator();
    Style& style = GetStyle();
    if (BeginGrid("theme-tokens", 2, 14.0f)) {
        TextDisabled("SURFACE + MOTION");
        SliderFloat("Window", &style.window_rounding, 2.0f, 20.0f, "%.1f");
        SliderFloat("Control", &style.frame_rounding, 1.0f, 14.0f, "%.1f");
        SliderFloat("Shadow", &style.shadow_size, 0.0f, 24.0f, "%.1f");
        SliderFloat("Motion", &style.animation_speed, 4.0f, 30.0f, "%.1f");

        NextGridColumn();
        TextDisabled("ACCENT RGB");
        U32 accent = style.colors[Col_Button];
        float r = Channel(accent, 0), g = Channel(accent, 8), b = Channel(accent, 16);
        bool changed = false;
        changed |= SliderFloat("Red", &r, 0.0f, 1.0f, "%.2f");
        changed |= SliderFloat("Green", &g, 0.0f, 1.0f, "%.2f");
        changed |= SliderFloat("Blue", &b, 0.0f, 1.0f, "%.2f");
        if (changed) {
            int rr = (int)(r * 255), gg = (int)(g * 255), bb = (int)(b * 255);
            int rh = rr + 22 < 255 ? rr + 22 : 255;
            int gh = gg + 22 < 255 ? gg + 22 : 255;
            int bh = bb + 22 < 255 ? bb + 22 : 255;
            style.colors[Col_Button] = OG_COL32(rr, gg, bb, 255);
            style.colors[Col_ButtonHovered] = OG_COL32(rh, gh, bh, 255);
            style.colors[Col_GradientStart] = style.colors[Col_Button];
            style.colors[Col_NodeTitle] = style.colors[Col_Button];
        }
        TextDisabled("FONT FAMILY");
        if (Button("Segoe UI", Vec2(92, 28))) OG_ImplWin32_SetFont("Segoe UI", 17, FW_NORMAL);
        SameLine();
        if (Button("Cascadia", Vec2(92, 28))) OG_ImplWin32_SetFont("Cascadia Mono", 16, FW_NORMAL);
        TextDisabled("%s / %d px", OG_ImplWin32_GetFontFamily(), OG_ImplWin32_GetFontSize());
        EndGrid();
    }

    TextColored(Vec4(0.45f, 0.88f, 0.78f, 1.0f), "GPU shader effects: %s",
                OG_ImplOpenGL2_HasShaderEffects() ? "available" : "fallback mode");
    End();
}

} // namespace og
