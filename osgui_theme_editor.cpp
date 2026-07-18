// Runtime theme, font, accessibility, and shader-effects studio.
#include "osgui.h"
#include "osgui_impl_opengl2.h"
#include "osgui_impl_win32.h"

namespace og {

static float ColorChannel(U32 color, int shift) {
    return (float)((color >> shift) & 255) / 255.0f;
}

void ShowThemeEditor(bool* p_open) {
    SetNextWindowPos(Vec2(920, 430), Cond_FirstUseEver);
    SetNextWindowSize(Vec2(485, 470), Cond_FirstUseEver);
    if (!Begin("OSGui / Theme Studio", p_open)) {
        End();
        return;
    }

    TextColored(Vec4(0.55f, 0.93f, 0.85f, 1.0f), "V2 DESIGN TOKENS");
    SameLine();
    StatusBadge(IsReducedMotion() ? "Reduced motion" : "Full motion",
                IsReducedMotion() ? GetColorU32(Col_Warning) : GetColorU32(Col_Success));
    TextDisabled("Every edit is applied to the running interface immediately.");

    if (Button("Midnight", Vec2(96, 32)))
        SetTheme(Theme_Dark, 0.3f);
    SameLine();
    if (Button("Daylight", Vec2(96, 32)))
        SetTheme(Theme_Light, 0.3f);
    SameLine();
    if (Button("High contrast", Vec2(118, 32)))
        SetTheme(Theme_HighContrast, 0.15f);

    bool reduced_motion = IsReducedMotion();
    if (Checkbox("Reduce non-essential animation", &reduced_motion))
        SetReducedMotion(reduced_motion);

    Separator();
    Style& style = GetStyle();
    if (BeginGrid("theme-tokens", 2, 14.0f)) {
        TextDisabled("SURFACE GEOMETRY");
        DragFloat("Window radius", &style.window_rounding, 0.15f, 0.0f, 24.0f, "%.1f px");
        DragFloat("Control radius", &style.frame_rounding, 0.15f, 0.0f, 18.0f, "%.1f px");
        DragFloat("Shadow reach", &style.shadow_size, 0.2f, 0.0f, 30.0f, "%.1f px");
        DragFloat("Disabled alpha", &style.disabled_alpha, 0.01f, 0.1f, 1.0f, "%.2f");

        NextGridColumn();

        TextDisabled("MOTION + ACCENT");
        BeginDisabled(reduced_motion);
        DragFloat("Motion scale", &style.motion_scale, 0.02f, 0.0f, 3.0f, "%.2fx");
        DragFloat("Response", &style.animation_speed, 0.25f, 1.0f, 36.0f, "%.1f");
        EndDisabled();

        U32 accent = style.colors[Col_Button];
        float red = ColorChannel(accent, 0);
        float green = ColorChannel(accent, 8);
        float blue = ColorChannel(accent, 16);
        bool changed = false;
        changed |= SliderFloat("Red", &red, 0.0f, 1.0f, "%.2f");
        changed |= SliderFloat("Green", &green, 0.0f, 1.0f, "%.2f");
        changed |= SliderFloat("Blue", &blue, 0.0f, 1.0f, "%.2f");
        if (changed) {
            const int r = (int)(red * 255.0f);
            const int g = (int)(green * 255.0f);
            const int b = (int)(blue * 255.0f);
            const int hover_r = r + 22 < 255 ? r + 22 : 255;
            const int hover_g = g + 22 < 255 ? g + 22 : 255;
            const int hover_b = b + 22 < 255 ? b + 22 : 255;
            style.colors[Col_Button] = OG_COL32(r, g, b, 255);
            style.colors[Col_ButtonHovered] = OG_COL32(hover_r, hover_g, hover_b, 255);
            style.colors[Col_GradientStart] = style.colors[Col_Button];
            style.colors[Col_NodeTitle] = style.colors[Col_Button];
        }

        EndGrid();
    }

    Separator();
    TextDisabled("FONT ATLAS");
    if (Button("Segoe UI", Vec2(96, 28)))
        OG_ImplWin32_SetFont("Segoe UI", 17, FW_NORMAL);
    SameLine();
    if (Button("Cascadia", Vec2(96, 28)))
        OG_ImplWin32_SetFont("Cascadia Mono", 16, FW_NORMAL);
    SameLine();
    TextDisabled("%s / %d px", OG_ImplWin32_GetFontFamily(), OG_ImplWin32_GetFontSize());

    if (OG_ImplOpenGL2_HasShaderEffects()) {
        StatusBadge("GPU blur available", GetColorU32(Col_Success), true);
    } else {
        StatusBadge("Translucent fallback", GetColorU32(Col_Warning));
    }
    SameLine();
    TextDisabled("OSGui %s", GetVersion());

    End();
}

} // namespace og
