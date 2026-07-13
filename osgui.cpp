// OSGui core implementation: layout, interaction, widgets, and draw data.
#include "osgui.h"
#include <math.h>
#include <string.h>
#include <stdio.h>
#include <stdarg.h>

namespace og {

// =====================================================================
//  Globals
// =====================================================================
static Context* GOG = 0;
Context& GetContext() { return *GOG; }
IO&        GetIO()        { return GOG->io; }
Style&     GetStyle()     { return GOG->style; }
FontAtlas& GetFontAtlas() { return GOG->atlas; }
DrawData*  GetDrawData()  { return &GOG->draw_data; }
const std::vector<Event>& GetEvents() { return GOG->events; }
U32        GetColorU32(int idx) { return GOG->style.colors[idx]; }

// =====================================================================
//  Window (persistent per-name state)
// =====================================================================
struct Window {
    ID          id;
    std::string name;
    Vec2        pos;
    Vec2        size;        // size used this frame
    Vec2        size_full;   // size when expanded
    bool        collapsed;
    bool        active_this_frame;
    int         focus_order;
    DockSlot    dock_slot;

    // layout cursor (absolute screen coords)
    Vec2        cursor;
    Vec2        cursor_start;
    Vec2        cursor_prev_line;
    Vec2        cursor_max;
    float       curr_line_height;
    float       prev_line_height;
    float       indent;
    float       content_w;

    float       scroll_y;
    float       scroll_max_y;
    bool        scrollbar_active;

    DrawList    draw;
    std::vector<ID> id_stack;

    Window() : id(0), collapsed(false), active_this_frame(false), focus_order(0), dock_slot(Dock_None),
               curr_line_height(0), prev_line_height(0), indent(0), content_w(0),
               scroll_y(0), scroll_max_y(0), scrollbar_active(false) {}
};

struct GridState {
    Window* window;
    Vec2 outer_cursor;
    Vec2 outer_cursor_start;
    float outer_content_w;
    float outer_indent;
    int columns;
    int column;
    float gap;
    float column_width;
    float row_y;
    float row_height;
};
static std::vector<GridState> GGridStack;
static DockSlot GNextDockSlot = Dock_None;
static bool GNextDockSet = false;

// =====================================================================
//  Small helpers
// =====================================================================
static inline float Min(float a, float b) { return a < b ? a : b; }
static inline float Max(float a, float b) { return a > b ? a : b; }
static inline float Clamp(float v, float lo, float hi) { return v < lo ? lo : (v > hi ? hi : v); }
static inline Vec2  Add(const Vec2& a, const Vec2& b) { return Vec2(a.x + b.x, a.y + b.y); }
static inline bool  PointIn(const Vec2& p, const Vec4& r) {
    return p.x >= r.x && p.y >= r.y && p.x < r.z && p.y < r.w;
}
static inline int ColR(U32 c) { return (int)(c & 255); }
static inline int ColG(U32 c) { return (int)((c >> 8) & 255); }
static inline int ColB(U32 c) { return (int)((c >> 16) & 255); }
static inline int ColA(U32 c) { return (int)((c >> 24) & 255); }
static U32 ColorLerp(U32 a, U32 b, float t) {
    t = Clamp(t, 0.0f, 1.0f);
    return OG_COL32((int)(ColR(a) + (ColR(b) - ColR(a)) * t),
                    (int)(ColG(a) + (ColG(b) - ColG(a)) * t),
                    (int)(ColB(a) + (ColB(b) - ColB(a)) * t),
                    (int)(ColA(a) + (ColA(b) - ColA(a)) * t));
}
static U32 ColorWithAlpha(U32 c, int alpha) {
    return (c & 0x00FFFFFFu) | ((U32)Clamp((float)alpha, 0.0f, 255.0f) << 24);
}
static ID HashStr(const char* s, const char* s_end, ID seed) {
    ID h = seed ? seed : 1469598103934665603ULL;
    if (!s_end) { while (*s) { h ^= (unsigned char)*s++; h *= 1099511628211ULL; } }
    else        { while (s < s_end) { h ^= (unsigned char)*s++; h *= 1099511628211ULL; } }
    return h;
}
static unsigned int DecodeUTF8(const char*& p, const char* end) {
    if (!p || (end && p >= end) || !*p) return 0;
    const unsigned char* s = (const unsigned char*)p;
    unsigned int cp = 0;
    int bytes = 1;
    if (s[0] < 0x80) cp = s[0];
    else if ((s[0] & 0xE0) == 0xC0) { cp = s[0] & 0x1F; bytes = 2; }
    else if ((s[0] & 0xF0) == 0xE0) { cp = s[0] & 0x0F; bytes = 3; }
    else if ((s[0] & 0xF8) == 0xF0) { cp = s[0] & 0x07; bytes = 4; }
    else { ++p; return 0xFFFD; }
    for (int i = 1; i < bytes; ++i) {
        if ((end && p + i >= end) || !s[i] || (s[i] & 0xC0) != 0x80) { ++p; return 0xFFFD; }
        cp = (cp << 6) | (s[i] & 0x3F);
    }
    p += bytes;
    return cp;
}
static const Glyph* FindGlyph(unsigned int cp) {
    FontAtlas& atlas = GOG->atlas;
    std::map<unsigned int, Glyph>::const_iterator found = atlas.glyph_map.find(cp);
    if (found != atlas.glyph_map.end()) return &found->second;
    if (cp < 128 && atlas.glyph_valid[cp]) return &atlas.glyphs[cp];
    found = atlas.glyph_map.find('?');
    if (found != atlas.glyph_map.end()) return &found->second;
    return atlas.glyph_valid[(unsigned int)'?'] ? &atlas.glyphs[(unsigned int)'?'] : 0;
}
// label "text##id": display ends at "##"; id hashes the full label.
static const char* FindDisplayEnd(const char* label) {
    const char* p = label;
    while (*p) { if (p[0] == '#' && p[1] == '#') return p; p++; }
    return p;
}
static ID GetID(const char* label) {
    Window* w = GOG->cur_window;
    return HashStr(label, 0, w->id_stack.back());
}
static float AnimateID(ID id, float target, float speed) {
    Context& g = *GOG;
    AnimationState& state = g.animations[id];
    if (state.last_frame == 0) state.value = target;
    state.target = target;
    state.last_frame = g.frame_count;
    if (speed <= 0.0f) speed = g.style.animation_speed;
    float dt = Clamp(g.io.delta_time, 0.0f, 0.05f);
    float blend = 1.0f - expf(-speed * dt);
    state.value += (state.target - state.value) * blend;
    if (fabsf(state.target - state.value) < 0.0005f) state.value = state.target;
    return state.value;
}
float Animate(const char* key, float target, float speed) {
    ID seed = (GOG->cur_window && !GOG->cur_window->id_stack.empty())
        ? GOG->cur_window->id_stack.back() : 0;
    return AnimateID(HashStr(key, 0, seed), target, speed);
}

// =====================================================================
//  DrawList
// =====================================================================
void DrawList::Clear() {
    vtx.clear(); idx.clear(); cmds.clear(); clip_stack.clear();
    cur_effect = DrawEffect_None;
    cur_effect_amount = 0.0f;
}
DrawCmd& DrawList::CurCmd() {
    if (cmds.empty()) {
        DrawCmd c;
        c.clip_rect = clip_stack.empty() ? Vec4(-8192, -8192, 8192, 8192) : clip_stack.back();
        c.tex_id = cur_tex; c.idx_offset = (unsigned)idx.size(); c.elem_count = 0;
        c.effect = cur_effect; c.effect_amount = cur_effect_amount;
        cmds.push_back(c);
    }
    return cmds.back();
}
static void NewCmd(DrawList* dl) {
    DrawCmd c;
    c.clip_rect = dl->clip_stack.empty() ? Vec4(-8192, -8192, 8192, 8192) : dl->clip_stack.back();
    c.tex_id = dl->cur_tex; c.idx_offset = (unsigned)dl->idx.size(); c.elem_count = 0;
    c.effect = dl->cur_effect; c.effect_amount = dl->cur_effect_amount;
    dl->cmds.push_back(c);
}
void DrawList::PushClipRect(const Vec4& r) { clip_stack.push_back(r); NewCmd(this); }
void DrawList::PopClipRect()               { if (!clip_stack.empty()) clip_stack.pop_back(); NewCmd(this); }
void DrawList::PrimReserve(int idx_count, int) { CurCmd().elem_count += idx_count; }
void DrawList::PushEffect(int effect, float amount) {
    cur_effect = effect;
    cur_effect_amount = amount;
    NewCmd(this);
}
void DrawList::PopEffect() {
    cur_effect = DrawEffect_None;
    cur_effect_amount = 0.0f;
    NewCmd(this);
}

void DrawList::AddRectFilled(const Vec2& a, const Vec2& b, U32 col) {
    DrawIdx base = (DrawIdx)vtx.size();
    PrimReserve(6, 4);
    DrawVert v; v.col = col; v.uv = white_uv;
    v.pos = Vec2(a.x, a.y); vtx.push_back(v);
    v.pos = Vec2(b.x, a.y); vtx.push_back(v);
    v.pos = Vec2(b.x, b.y); vtx.push_back(v);
    v.pos = Vec2(a.x, b.y); vtx.push_back(v);
    idx.push_back(base + 0); idx.push_back(base + 1); idx.push_back(base + 2);
    idx.push_back(base + 0); idx.push_back(base + 2); idx.push_back(base + 3);
}
void DrawList::AddRectFilledRounded(const Vec2& a, const Vec2& b, U32 col, float radius) {
    float w = b.x - a.x, h = b.y - a.y;
    float r = Min(radius, Min(w, h) * 0.5f);
    if (r <= 1.0f) { AddRectFilled(a, b, col); return; }
    AddRectFilled(Vec2(a.x + r, a.y), Vec2(b.x - r, b.y), col);
    AddRectFilled(Vec2(a.x, a.y + r), Vec2(b.x, b.y - r), col);
    AddCircleFilled(Vec2(a.x + r, a.y + r), r, col, 10);
    AddCircleFilled(Vec2(b.x - r, a.y + r), r, col, 10);
    AddCircleFilled(Vec2(a.x + r, b.y - r), r, col, 10);
    AddCircleFilled(Vec2(b.x - r, b.y - r), r, col, 10);
}
void DrawList::AddRectFilledMultiColor(const Vec2& a, const Vec2& b,
                                       U32 col_tl, U32 col_tr, U32 col_br, U32 col_bl) {
    DrawIdx base = (DrawIdx)vtx.size();
    PrimReserve(6, 4);
    DrawVert v; v.uv = white_uv;
    v.pos = Vec2(a.x, a.y); v.col = col_tl; vtx.push_back(v);
    v.pos = Vec2(b.x, a.y); v.col = col_tr; vtx.push_back(v);
    v.pos = Vec2(b.x, b.y); v.col = col_br; vtx.push_back(v);
    v.pos = Vec2(a.x, b.y); v.col = col_bl; vtx.push_back(v);
    idx.push_back(base + 0); idx.push_back(base + 1); idx.push_back(base + 2);
    idx.push_back(base + 0); idx.push_back(base + 2); idx.push_back(base + 3);
}
void DrawList::AddShadowRect(const Vec2& a, const Vec2& b, U32 col, float radius, float spread) {
    const int layers = 5;
    for (int i = layers; i >= 1; --i) {
        float t = (float)i / (float)layers;
        float grow = spread * t;
        int alpha = (int)(ColA(col) * (1.0f - t * 0.78f) / layers);
        AddRectFilledRounded(Vec2(a.x - grow, a.y - grow), Vec2(b.x + grow, b.y + grow),
                             ColorWithAlpha(col, alpha), radius + grow);
    }
}
void DrawList::AddBackdropBlur(const Vec2& a, const Vec2& b, U32 tint, float radius, float rounding) {
    PushEffect(DrawEffect_BackdropBlur, radius);
    AddRectFilledRounded(a, b, tint, rounding);
    PopEffect();
}
void DrawList::AddQuad(const Vec2& a, const Vec2& b, const Vec2& c, const Vec2& d, U32 col) {
    DrawIdx base = (DrawIdx)vtx.size();
    PrimReserve(6, 4);
    DrawVert v; v.col = col; v.uv = white_uv;
    v.pos = a; vtx.push_back(v);
    v.pos = b; vtx.push_back(v);
    v.pos = c; vtx.push_back(v);
    v.pos = d; vtx.push_back(v);
    idx.push_back(base + 0); idx.push_back(base + 1); idx.push_back(base + 2);
    idx.push_back(base + 0); idx.push_back(base + 2); idx.push_back(base + 3);
}
void DrawList::AddRect(const Vec2& a, const Vec2& b, U32 col, float th) {
    AddRectFilled(Vec2(a.x, a.y), Vec2(b.x, a.y + th), col);     // top
    AddRectFilled(Vec2(a.x, b.y - th), Vec2(b.x, b.y), col);     // bottom
    AddRectFilled(Vec2(a.x, a.y), Vec2(a.x + th, b.y), col);     // left
    AddRectFilled(Vec2(b.x - th, a.y), Vec2(b.x, b.y), col);     // right
}
void DrawList::AddLine(const Vec2& a, const Vec2& b, U32 col, float th) {
    float dx = b.x - a.x, dy = b.y - a.y;
    float len = sqrtf(dx * dx + dy * dy);
    if (len < 1e-4f) return;
    dx /= len; dy /= len;
    float nx = -dy * th * 0.5f, ny = dx * th * 0.5f;
    AddQuad(Vec2(a.x + nx, a.y + ny), Vec2(b.x + nx, b.y + ny),
            Vec2(b.x - nx, b.y - ny), Vec2(a.x - nx, a.y - ny), col);
}
void DrawList::AddTriangleFilled(const Vec2& a, const Vec2& b, const Vec2& c, U32 col) {
    DrawIdx base = (DrawIdx)vtx.size();
    PrimReserve(3, 3);
    DrawVert v; v.col = col; v.uv = white_uv;
    v.pos = a; vtx.push_back(v);
    v.pos = b; vtx.push_back(v);
    v.pos = c; vtx.push_back(v);
    idx.push_back(base + 0); idx.push_back(base + 1); idx.push_back(base + 2);
}
void DrawList::AddCircleFilled(const Vec2& c, float r, U32 col, int segs) {
    for (int i = 0; i < segs; i++) {
        float a0 = (float)i / segs * 6.2831853f;
        float a1 = (float)(i + 1) / segs * 6.2831853f;
        AddTriangleFilled(c, Vec2(c.x + cosf(a0) * r, c.y + sinf(a0) * r),
                             Vec2(c.x + cosf(a1) * r, c.y + sinf(a1) * r), col);
    }
}
void DrawList::AddGlyph(float x0, float y0, float x1, float y1,
                        float u0, float v0, float u1, float v1, U32 col) {
    DrawIdx base = (DrawIdx)vtx.size();
    PrimReserve(6, 4);
    DrawVert v;  v.col = col;
    v.pos = Vec2(x0, y0); v.uv = Vec2(u0, v0); vtx.push_back(v);
    v.pos = Vec2(x1, y0); v.uv = Vec2(u1, v0); vtx.push_back(v);
    v.pos = Vec2(x1, y1); v.uv = Vec2(u1, v1); vtx.push_back(v);
    v.pos = Vec2(x0, y1); v.uv = Vec2(u0, v1); vtx.push_back(v);
    idx.push_back(base + 0); idx.push_back(base + 1); idx.push_back(base + 2);
    idx.push_back(base + 0); idx.push_back(base + 2); idx.push_back(base + 3);
}
void DrawList::AddText(const Vec2& pos, U32 col, const char* text, const char* text_end) {
    FontAtlas& a = GOG->atlas;
    float x = pos.x, y = pos.y;
    for (const char* p = text; (text_end ? p < text_end : *p) && *p; ) {
        unsigned int cp = DecodeUTF8(p, text_end);
        if (cp == '\n') { x = pos.x; y += a.line_height; continue; }
        if (cp < 32) continue;
        const Glyph* glyph = FindGlyph(cp);
        if (!glyph) continue;
        if (cp != ' ')
            AddGlyph(x + glyph->x0, y + glyph->y0, x + glyph->x1, y + glyph->y1,
                     glyph->u0, glyph->v0, glyph->u1, glyph->v1, col);
        x += glyph->advance;
    }
}

// =====================================================================
//  Style
// =====================================================================
Style::Style() {
    window_padding   = Vec2(18, 16);
    frame_padding    = Vec2(10, 6);
    item_spacing     = Vec2(10, 9);
    item_inner_spacing = Vec2(10, 6);
    indent_spacing   = 24.0f;
    scrollbar_size   = 10.0f;
    grab_min_size    = 14.0f;
    window_title_height = 0.0f;
    window_rounding = 10.0f;
    frame_rounding = 6.0f;
    shadow_size = 12.0f;
    animation_speed = 14.0f;
    colors[Col_Text]              = OG_COL32(237, 240, 249, 255);
    colors[Col_TextDisabled]      = OG_COL32(139, 146, 170, 255);
    colors[Col_WindowBg]          = OG_COL32(22, 24, 36, 250);
    colors[Col_TitleBg]           = OG_COL32(27, 29, 43, 255);
    colors[Col_TitleBgActive]     = OG_COL32(31, 34, 50, 255);
    colors[Col_MenuBarBg]         = OG_COL32(8, 10, 18, 255);
    colors[Col_Border]            = OG_COL32(59, 63, 86, 210);
    colors[Col_FrameBg]           = OG_COL32(38, 41, 58, 255);
    colors[Col_FrameBgHovered]    = OG_COL32(49, 53, 74, 255);
    colors[Col_FrameBgActive]     = OG_COL32(58, 62, 86, 255);
    colors[Col_Button]            = OG_COL32(116, 92, 255, 255);
    colors[Col_ButtonHovered]     = OG_COL32(133, 113, 255, 255);
    colors[Col_ButtonActive]      = OG_COL32(96, 73, 235, 255);
    colors[Col_Header]            = OG_COL32(35, 38, 55, 255);
    colors[Col_HeaderHovered]     = OG_COL32(48, 51, 72, 255);
    colors[Col_HeaderActive]      = OG_COL32(56, 59, 82, 255);
    colors[Col_CheckMark]         = OG_COL32(114, 226, 204, 255);
    colors[Col_SliderGrab]        = OG_COL32(126, 105, 255, 255);
    colors[Col_SliderGrabActive]  = OG_COL32(139, 238, 218, 255);
    colors[Col_Separator]         = OG_COL32(110, 110, 128, 128);
    colors[Col_ResizeGrip]        = OG_COL32(66, 150, 250, 51);
    colors[Col_ResizeGripHovered] = OG_COL32(66, 150, 250, 171);
    colors[Col_ResizeGripActive]  = OG_COL32(66, 150, 250, 242);
    colors[Col_ScrollbarBg]       = OG_COL32(5, 5, 5, 135);
    colors[Col_ScrollbarGrab]     = OG_COL32(79, 79, 79, 255);
    colors[Col_PlotLines]         = OG_COL32(114, 226, 204, 255);
    colors[Col_PlotHistogram]     = OG_COL32(126, 105, 255, 255);
    colors[Col_WindowShadow]      = OG_COL32(0, 0, 0, 150);
    colors[Col_GradientStart]     = OG_COL32(126, 105, 255, 255);
    colors[Col_GradientEnd]       = OG_COL32(91, 210, 231, 255);
    colors[Col_CodeBg]            = OG_COL32(12, 14, 24, 255);
    colors[Col_Link]              = OG_COL32(111, 205, 255, 255);
    colors[Col_Success]           = OG_COL32(114, 226, 204, 255);
    colors[Col_Warning]           = OG_COL32(247, 193, 96, 255);
    colors[Col_NodeBg]            = OG_COL32(30, 33, 49, 245);
    colors[Col_NodeTitle]         = OG_COL32(83, 67, 172, 255);
    colors[Col_NodeGrid]          = OG_COL32(74, 79, 105, 80);
    colors[Col_NodeLink]          = OG_COL32(114, 226, 204, 255);
}

Style GetBuiltinTheme(ThemePreset preset) {
    Style s;
    if (preset == Theme_Dark) return s;

    s.colors[Col_Text]              = OG_COL32(28, 31, 43, 255);
    s.colors[Col_TextDisabled]      = OG_COL32(104, 110, 130, 255);
    s.colors[Col_WindowBg]          = OG_COL32(244, 246, 252, 252);
    s.colors[Col_TitleBg]           = OG_COL32(233, 236, 246, 255);
    s.colors[Col_TitleBgActive]     = OG_COL32(239, 241, 250, 255);
    s.colors[Col_MenuBarBg]         = OG_COL32(224, 228, 240, 255);
    s.colors[Col_Border]            = OG_COL32(195, 201, 218, 220);
    s.colors[Col_FrameBg]           = OG_COL32(224, 228, 240, 255);
    s.colors[Col_FrameBgHovered]    = OG_COL32(211, 216, 233, 255);
    s.colors[Col_FrameBgActive]     = OG_COL32(199, 205, 225, 255);
    s.colors[Col_Header]            = OG_COL32(229, 232, 243, 255);
    s.colors[Col_HeaderHovered]     = OG_COL32(214, 218, 235, 255);
    s.colors[Col_HeaderActive]      = OG_COL32(202, 207, 228, 255);
    s.colors[Col_Separator]         = OG_COL32(183, 189, 207, 180);
    s.colors[Col_ScrollbarBg]       = OG_COL32(225, 228, 237, 180);
    s.colors[Col_ScrollbarGrab]     = OG_COL32(166, 173, 196, 255);
    s.colors[Col_WindowShadow]      = OG_COL32(39, 45, 70, 90);
    s.colors[Col_CodeBg]            = OG_COL32(224, 227, 238, 255);
    s.colors[Col_NodeBg]            = OG_COL32(250, 251, 255, 250);
    s.colors[Col_NodeTitle]         = OG_COL32(118, 92, 255, 255);
    s.colors[Col_NodeGrid]          = OG_COL32(148, 155, 180, 80);
    s.colors[Col_NodeLink]          = OG_COL32(43, 184, 159, 255);
    return s;
}

static void ScaleStyleMetrics(Style& s, float scale) {
    s.window_padding.x *= scale; s.window_padding.y *= scale;
    s.frame_padding.x *= scale; s.frame_padding.y *= scale;
    s.item_spacing.x *= scale; s.item_spacing.y *= scale;
    s.item_inner_spacing.x *= scale; s.item_inner_spacing.y *= scale;
    s.indent_spacing *= scale;
    s.scrollbar_size *= scale;
    s.grab_min_size *= scale;
    s.window_rounding *= scale;
    s.frame_rounding *= scale;
    s.shadow_size *= scale;
}

void SetTheme(ThemePreset preset, float transition_seconds) {
    Context& g = *GOG;
    g.theme_from = g.style;
    g.theme_target = GetBuiltinTheme(preset);
    if (g.ui_scale != 1.0f) ScaleStyleMetrics(g.theme_target, g.ui_scale);
    g.theme_elapsed = 0.0f;
    g.theme_duration = Max(transition_seconds, 0.0f);
    g.theme_transitioning = g.theme_duration > 0.0f;
    g.theme_preset = preset;
    if (!g.theme_transitioning) g.style = g.theme_target;
}
bool IsThemeTransitioning() { return GOG && GOG->theme_transitioning; }

void SetUIScale(float scale) {
    if (!GOG) return;
    scale = Clamp(scale, 0.75f, 3.0f);
    Context& g = *GOG;
    if (fabsf(scale - g.ui_scale) < 0.001f) return;
    float ratio = scale / g.ui_scale;
    ScaleStyleMetrics(g.style, ratio);
    ScaleStyleMetrics(g.theme_from, ratio);
    ScaleStyleMetrics(g.theme_target, ratio);
    for (size_t i = 0; i < g.windows.size(); ++i) {
        g.windows[i]->pos.x *= ratio; g.windows[i]->pos.y *= ratio;
        g.windows[i]->size_full.x *= ratio; g.windows[i]->size_full.y *= ratio;
    }
    g.ui_scale = scale;
}
float GetUIScale() { return GOG ? GOG->ui_scale : 1.0f; }
bool IsKeyPressed(int key) { return GOG && key >= 0 && key < 256 && GOG->key_pressed[key]; }

Context::Context() {
    cur_window = hovered_window = moving_window = nav_window = 0;
    active_id = 0; text_active_id = 0; active_id_window = 0; hovered_id = 0;
    frame_count = 0; focus_counter = 0; time = 0; framerate_acc = 60.0f;
    for (int i = 0; i < 3; i++) { mouse_down_prev[i] = mouse_clicked[i] = mouse_released[i] = false; }
    for (int i = 0; i < 256; ++i) { key_down_prev[i] = key_pressed[i] = false; }
    next_pos_set = next_size_set = false;
    memset(&io, 0, sizeof(io));
    io.framerate = 60.0f;
    io.framebuffer_scale = Vec2(1.0f, 1.0f);
    io.dpi_scale = 1.0f;
    atlas.pixels = 0; atlas.tex_id = 0;
    theme_from = style;
    theme_target = style;
    theme_elapsed = 0.0f;
    theme_duration = 0.0f;
    theme_transitioning = false;
    theme_preset = Theme_Dark;
    ui_scale = 1.0f;
    nav_id = nav_activate_id = 0;
}

StreamingSeries::StreamingSeries(int capacity)
    : values_((size_t)Max((float)capacity, 1.0f)), head_(0), count_(0) {}
void StreamingSeries::Push(float value) {
    values_[(size_t)head_] = value;
    head_ = (head_ + 1) % (int)values_.size();
    if (count_ < (int)values_.size()) ++count_;
}
void StreamingSeries::Clear() { head_ = 0; count_ = 0; }
int StreamingSeries::Size() const { return count_; }
int StreamingSeries::Capacity() const { return (int)values_.size(); }
void StreamingSeries::GetOrdered(std::vector<float>& out) const {
    out.resize((size_t)count_);
    int start = (head_ - count_ + (int)values_.size()) % (int)values_.size();
    for (int i = 0; i < count_; ++i)
        out[(size_t)i] = values_[(size_t)((start + i) % (int)values_.size())];
}

// =====================================================================
//  Context lifecycle
// =====================================================================
Context* CreateContext() { GOG = new Context(); return GOG; }
void DestroyContext() {
    if (!GOG) return;
    for (size_t i = 0; i < GOG->windows.size(); i++) delete GOG->windows[i];
    delete GOG; GOG = 0;
}

static Window* FindWindow(const char* name) {
    ID id = HashStr(name, 0, 0);
    for (size_t i = 0; i < GOG->windows.size(); i++)
        if (GOG->windows[i]->id == id) return GOG->windows[i];
    return 0;
}
static Window* CreateWindowObj(const char* name) {
    Window* w = new Window();
    w->name = name;
    w->id = HashStr(name, 0, 0);
    // cascade newly created windows
    int n = (int)GOG->windows.size();
    w->pos  = Vec2(40.0f + n * 28.0f, 40.0f + n * 28.0f);
    w->size_full = Vec2(380, 420);
    w->focus_order = ++GOG->focus_counter;
    GOG->windows.push_back(w);
    return w;
}
static void FocusWindow(Window* w) {
    w->focus_order = ++GOG->focus_counter;
    GOG->nav_window = w;
}

// =====================================================================
//  Frame lifecycle
// =====================================================================
void NewFrame() {
    Context& g = *GOG;
    IO& io = g.io;
    g.frame_count++;
    g.time += io.delta_time;
    g.events.clear();
    g.overlay_draw.Clear();
    g.overlay_draw.cur_tex = g.atlas.tex_id;
    g.overlay_draw.white_uv = g.atlas.white_uv;
    g.overlay_draw.PushClipRect(Vec4(0, 0, io.display_size.x, io.display_size.y));

    // Keyboard transitions and the previous frame's focus order form a stable
    // immediate-mode navigation list. Widgets register themselves again below.
    for (int i = 0; i < 256; ++i)
        g.key_pressed[i] = io.key_down[i] && !g.key_down_prev[i];
    g.nav_order_prev = g.nav_order;
    g.nav_order.clear();
    g.nav_activate_id = 0;
    if (g.key_pressed[9] && !g.nav_order_prev.empty()) { // Tab / Shift+Tab
        g.text_active_id = 0;
        int current = -1;
        for (size_t i = 0; i < g.nav_order_prev.size(); ++i)
            if (g.nav_order_prev[i] == g.nav_id) { current = (int)i; break; }
        int direction = io.key_down[16] ? -1 : 1;
        int count = (int)g.nav_order_prev.size();
        current = (current < 0) ? (direction > 0 ? 0 : count - 1)
                                : (current + direction + count) % count;
        g.nav_id = g.nav_order_prev[(size_t)current];
    }
    if ((g.key_pressed[13] || g.key_pressed[32]) && g.nav_id)
        g.nav_activate_id = g.nav_id;

    if (g.theme_transitioning) {
        g.theme_elapsed += Clamp(io.delta_time, 0.0f, 0.05f);
        float t = g.theme_duration > 0.0f ? Clamp(g.theme_elapsed / g.theme_duration, 0.0f, 1.0f) : 1.0f;
        // Smoothstep makes theme changes feel deliberate without overshoot.
        t = t * t * (3.0f - 2.0f * t);
        g.style.window_padding = Vec2(g.theme_from.window_padding.x + (g.theme_target.window_padding.x - g.theme_from.window_padding.x) * t,
                                      g.theme_from.window_padding.y + (g.theme_target.window_padding.y - g.theme_from.window_padding.y) * t);
        g.style.frame_padding = Vec2(g.theme_from.frame_padding.x + (g.theme_target.frame_padding.x - g.theme_from.frame_padding.x) * t,
                                     g.theme_from.frame_padding.y + (g.theme_target.frame_padding.y - g.theme_from.frame_padding.y) * t);
        g.style.item_spacing = Vec2(g.theme_from.item_spacing.x + (g.theme_target.item_spacing.x - g.theme_from.item_spacing.x) * t,
                                    g.theme_from.item_spacing.y + (g.theme_target.item_spacing.y - g.theme_from.item_spacing.y) * t);
        g.style.item_inner_spacing = Vec2(g.theme_from.item_inner_spacing.x + (g.theme_target.item_inner_spacing.x - g.theme_from.item_inner_spacing.x) * t,
                                          g.theme_from.item_inner_spacing.y + (g.theme_target.item_inner_spacing.y - g.theme_from.item_inner_spacing.y) * t);
        g.style.indent_spacing = g.theme_from.indent_spacing + (g.theme_target.indent_spacing - g.theme_from.indent_spacing) * t;
        g.style.scrollbar_size = g.theme_from.scrollbar_size + (g.theme_target.scrollbar_size - g.theme_from.scrollbar_size) * t;
        g.style.grab_min_size = g.theme_from.grab_min_size + (g.theme_target.grab_min_size - g.theme_from.grab_min_size) * t;
        g.style.window_rounding = g.theme_from.window_rounding + (g.theme_target.window_rounding - g.theme_from.window_rounding) * t;
        g.style.frame_rounding = g.theme_from.frame_rounding + (g.theme_target.frame_rounding - g.theme_from.frame_rounding) * t;
        g.style.shadow_size = g.theme_from.shadow_size + (g.theme_target.shadow_size - g.theme_from.shadow_size) * t;
        g.style.animation_speed = g.theme_from.animation_speed + (g.theme_target.animation_speed - g.theme_from.animation_speed) * t;
        for (int i = 0; i < Col_COUNT; ++i)
            g.style.colors[i] = ColorLerp(g.theme_from.colors[i], g.theme_target.colors[i], t);
        if (g.theme_elapsed >= g.theme_duration) {
            g.style = g.theme_target;
            g.theme_transitioning = false;
        }
    }

    if ((g.frame_count % 120) == 0) {
        for (std::map<ID, AnimationState>::iterator it = g.animations.begin(); it != g.animations.end(); ) {
            if (g.frame_count - it->second.last_frame > 240) it = g.animations.erase(it);
            else ++it;
        }
    }

    // smoothed framerate
    if (io.delta_time > 0)
        g.framerate_acc = g.framerate_acc * 0.95f + (1.0f / io.delta_time) * 0.05f;
    io.framerate = g.framerate_acc;

    g.style.window_title_height = g.atlas.line_height + g.style.frame_padding.y * 2.0f;

    // mouse transitions
    for (int i = 0; i < 3; i++) {
        g.mouse_clicked[i]  = io.mouse_down[i] && !g.mouse_down_prev[i];
        g.mouse_released[i] = !io.mouse_down[i] && g.mouse_down_prev[i];
    }

    g.hovered_id = 0;

    // determine hovered window: topmost (highest focus_order) under the mouse
    g.hovered_window = 0;
    int best = -1;
    for (size_t i = 0; i < g.windows.size(); i++) {
        Window* w = g.windows[i];
        if (!w->active_this_frame) continue;            // not shown last frame
        Vec4 r(w->pos.x, w->pos.y, w->pos.x + w->size.x, w->pos.y + w->size.y);
        if (PointIn(io.mouse_pos, r) && w->focus_order > best) { best = w->focus_order; g.hovered_window = w; }
    }

    // active widget keeps its window "hovered" for drag continuation
    if (g.active_id && g.active_id_window) g.hovered_window = g.active_id_window;

    // window moving
    if (g.moving_window) {
        if (io.mouse_down[0]) {
            Vec2 d(io.mouse_pos.x - g.mouse_pos_prev.x, io.mouse_pos.y - g.mouse_pos_prev.y);
            g.moving_window->pos.x += d.x;
            g.moving_window->pos.y += d.y;
        } else {
            g.moving_window = 0;
        }
    }

    // mark all windows not-active until Begin() touches them
    for (size_t i = 0; i < g.windows.size(); i++) g.windows[i]->active_this_frame = false;

    io.want_capture_mouse = (g.hovered_window != 0) || (g.active_id != 0);
    io.want_capture_keyboard = (g.nav_id != 0) || (g.text_active_id != 0);
}

void Render() {
    Context& g = *GOG;
    DrawData& dd = g.draw_data;
    dd.lists.clear();
    dd.display_pos = Vec2(0, 0);
    dd.display_size = g.io.display_size;
    dd.total_vtx = dd.total_idx = 0;

    // sort active windows by focus_order ascending (topmost drawn last)
    std::vector<Window*> order;
    for (size_t i = 0; i < g.windows.size(); i++)
        if (g.windows[i]->active_this_frame) order.push_back(g.windows[i]);
    for (size_t i = 0; i < order.size(); i++)
        for (size_t j = i + 1; j < order.size(); j++)
            if (order[j]->focus_order < order[i]->focus_order) { Window* t = order[i]; order[i] = order[j]; order[j] = t; }

    for (size_t i = 0; i < order.size(); i++) {
        DrawList* dl = &order[i]->draw;
        if (dl->idx.empty()) continue;
        dd.lists.push_back(dl);
        dd.total_vtx += (int)dl->vtx.size();
        dd.total_idx += (int)dl->idx.size();
    }
    if (!g.overlay_draw.idx.empty()) {
        dd.lists.push_back(&g.overlay_draw);
        dd.total_vtx += (int)g.overlay_draw.vtx.size();
        dd.total_idx += (int)g.overlay_draw.idx.size();
    }

    // store mouse for next-frame delta
    for (int i = 0; i < 3; i++) g.mouse_down_prev[i] = g.io.mouse_down[i];
    for (int i = 0; i < 256; ++i) g.key_down_prev[i] = g.io.key_down[i];
    g.mouse_pos_prev = g.io.mouse_pos;
    g.io.mouse_wheel = 0.0f;
    g.io.input_char_count = 0;
}

// =====================================================================
//  Text measuring
// =====================================================================
Vec2 CalcTextSize(const char* text, const char* text_end) {
    FontAtlas& a = GOG->atlas;
    float line_w = 0, max_w = 0, h = a.line_height;
    for (const char* p = text; (text_end ? p < text_end : *p) && *p; ) {
        unsigned int cp = DecodeUTF8(p, text_end);
        if (cp == '\n') { if (line_w > max_w) max_w = line_w; line_w = 0; h += a.line_height; continue; }
        const Glyph* glyph = FindGlyph(cp);
        if (glyph) line_w += glyph->advance;
    }
    if (line_w > max_w) max_w = line_w;
    return Vec2(max_w, h);
}

// =====================================================================
//  Layout primitives
// =====================================================================
static void ItemSize(const Vec2& size) {
    Window* w = GOG->cur_window;
    Style& s = GOG->style;
    float line_h = Max(w->curr_line_height, size.y);
    w->cursor_prev_line = Vec2(w->cursor.x + size.x, w->cursor.y);
    w->cursor.x = w->cursor_start.x + w->indent;
    w->cursor.y = w->cursor.y + line_h + s.item_spacing.y;
    w->prev_line_height = line_h;
    w->curr_line_height = 0;
    if (w->cursor_prev_line.x > w->cursor_max.x) w->cursor_max.x = w->cursor_prev_line.x;
    if (w->cursor.y - s.item_spacing.y > w->cursor_max.y) w->cursor_max.y = w->cursor.y - s.item_spacing.y;
}

void SameLine(float offset_x, float spacing) {
    Window* w = GOG->cur_window;
    Style& s = GOG->style;
    if (spacing < 0) spacing = s.item_spacing.x;
    if (offset_x > 0) w->cursor.x = w->cursor_start.x + offset_x;
    else              w->cursor.x = w->cursor_prev_line.x + spacing;
    w->cursor.y = w->cursor_prev_line.y;
    w->curr_line_height = w->prev_line_height;
}
void Spacing() { ItemSize(Vec2(0, GOG->atlas.line_height * 0.4f)); }
void Indent(float amt)   { Window* w = GOG->cur_window; w->indent += (amt > 0 ? amt : GOG->style.indent_spacing); w->cursor.x = w->cursor_start.x + w->indent; }
void Unindent(float amt) { Window* w = GOG->cur_window; w->indent -= (amt > 0 ? amt : GOG->style.indent_spacing); if (w->indent < 0) w->indent = 0; w->cursor.x = w->cursor_start.x + w->indent; }

Vec2 GetContentRegionAvail() {
    Window* w = GOG->cur_window;
    float used = w->cursor.x - w->cursor_start.x;
    return Vec2(w->content_w - used, 0);
}
bool BeginGrid(const char* id, int columns, float gap) {
    if (!GOG || !GOG->cur_window || columns < 1) return false;
    Window* w = GOG->cur_window;
    GridState grid;
    grid.window = w;
    grid.outer_cursor = w->cursor;
    grid.outer_cursor_start = w->cursor_start;
    grid.outer_content_w = w->content_w;
    grid.outer_indent = w->indent;
    grid.columns = columns;
    grid.column = 0;
    grid.gap = Max(gap, 0.0f);
    float available = Max(GetContentRegionAvail().x, (float)columns);
    grid.column_width = Max((available - grid.gap * (columns - 1)) / columns, 1.0f);
    grid.row_y = w->cursor.y;
    grid.row_height = 0.0f;
    GGridStack.push_back(grid);

    w->id_stack.push_back(HashStr(id ? id : "grid", 0, w->id_stack.back()));
    w->cursor_start = Vec2(grid.outer_cursor.x, grid.row_y);
    w->cursor = w->cursor_start;
    w->indent = 0.0f;
    w->content_w = grid.column_width;
    return true;
}
void NextGridColumn() {
    if (GGridStack.empty()) return;
    GridState& grid = GGridStack.back();
    Window* w = grid.window;
    float used = Max(0.0f, w->cursor.y - grid.row_y - GOG->style.item_spacing.y);
    grid.row_height = Max(grid.row_height, used);
    ++grid.column;
    if (grid.column >= grid.columns) {
        grid.column = 0;
        grid.row_y += grid.row_height + grid.gap;
        grid.row_height = 0.0f;
    }
    float x = grid.outer_cursor.x + grid.column * (grid.column_width + grid.gap);
    w->cursor_start = Vec2(x, grid.row_y);
    w->cursor = w->cursor_start;
    w->cursor_prev_line = w->cursor;
    w->curr_line_height = w->prev_line_height = 0.0f;
    w->indent = 0.0f;
    w->content_w = grid.column_width;
}
void EndGrid() {
    if (GGridStack.empty()) return;
    GridState grid = GGridStack.back();
    GGridStack.pop_back();
    Window* w = grid.window;
    float used = Max(0.0f, w->cursor.y - grid.row_y - GOG->style.item_spacing.y);
    grid.row_height = Max(grid.row_height, used);
    float total_height = (grid.row_y - grid.outer_cursor.y) + grid.row_height;

    if (w->id_stack.size() > 1) w->id_stack.pop_back();
    w->cursor_start = grid.outer_cursor_start;
    w->content_w = grid.outer_content_w;
    w->indent = grid.outer_indent;
    w->cursor = grid.outer_cursor;
    w->cursor_prev_line = w->cursor;
    w->curr_line_height = w->prev_line_height = 0.0f;
    ItemSize(Vec2(grid.outer_content_w, total_height));
}
static float CalcItemWidth() {
    Window* w = GOG->cur_window;
    float fw = w->content_w * 0.62f;
    if (fw < 60) fw = 60;
    return fw;
}

// =====================================================================
//  Interaction
// =====================================================================
static bool ItemHoverable(const Vec4& r, ID id) {
    Context& g = *GOG;
    Window* w = g.cur_window;
    if (g.hovered_window != w) return false;
    if (g.active_id && g.active_id != id) return false;
    if (!PointIn(g.io.mouse_pos, r)) return false;
    // clip against window body
    Vec4 body(w->pos.x, w->pos.y, w->pos.x + w->size.x, w->pos.y + w->size.y);
    if (!PointIn(g.io.mouse_pos, body)) return false;
    g.hovered_id = id;
    return true;
}
static void RegisterFocusable(ID id) {
    Context& g = *GOG;
    g.nav_order.push_back(id);
    if (!g.nav_id) g.nav_id = id;
}
static bool ButtonBehavior(const Vec4& r, ID id, bool* out_hovered, bool* out_held) {
    Context& g = *GOG;
    RegisterFocusable(id);
    bool hovered = ItemHoverable(r, id);
    bool pressed = false, held = false;
    if (hovered && g.mouse_clicked[0]) {
        g.active_id = id; g.active_id_window = g.cur_window; g.nav_id = id;
        g.text_active_id = 0; FocusWindow(g.cur_window);
    }
    if (g.active_id == id) {
        if (g.io.mouse_down[0]) held = true;
        else { if (hovered) pressed = true; g.active_id = 0; }
    }
    if (g.nav_activate_id == id) pressed = true;
    if (out_hovered) *out_hovered = hovered;
    if (out_held)    *out_held = held;
    return pressed;
}
static void RenderArrow(DrawList* dl, const Vec2& corner, float sz, U32 col, int dir) {
    // dir: 0 = right (collapsed), 1 = down (open)
    if (dir == 1) dl->AddTriangleFilled(Vec2(corner.x, corner.y), Vec2(corner.x + sz, corner.y),
                                        Vec2(corner.x + sz * 0.5f, corner.y + sz * 0.85f), col);
    else          dl->AddTriangleFilled(Vec2(corner.x, corner.y), Vec2(corner.x, corner.y + sz),
                                        Vec2(corner.x + sz * 0.85f, corner.y + sz * 0.5f), col);
}
static void RenderTextClipped(const Vec4& rect, const char* text, const char* end, bool center) {
    Window* w = GOG->cur_window;
    Vec2 sz = CalcTextSize(text, end);
    float tx = center ? (rect.x + (rect.z - rect.x - sz.x) * 0.5f) : rect.x;
    float ty = rect.y + ((rect.w - rect.y) - sz.y) * 0.5f;
    w->draw.AddText(Vec2(tx, ty), GetColorU32(Col_Text), text, end);
}

// =====================================================================
//  Begin / End
// =====================================================================
void SetNextWindowPos(const Vec2& pos)   { GOG->next_pos_set = true;  GOG->next_pos = pos; }
void SetNextWindowSize(const Vec2& size) { GOG->next_size_set = true; GOG->next_size = size; }
void SetNextWindowDock(DockSlot slot) { GNextDockSlot = slot; GNextDockSet = true; }
void DockWindow(const char* name, DockSlot slot) {
    Window* w = name ? FindWindow(name) : 0;
    if (w) w->dock_slot = slot;
}

bool Begin(const char* name, bool* p_open) {
    Context& g = *GOG;
    Style& s = g.style;
    Window* w = FindWindow(name);
    bool created = false;
    if (!w) { w = CreateWindowObj(name); created = true; }
    if (created && g.next_pos_set)  w->pos = g.next_pos;
    if (created && g.next_size_set) w->size_full = g.next_size;
    if (GNextDockSet) w->dock_slot = GNextDockSlot;
    g.next_pos_set = g.next_size_set = false;
    GNextDockSet = false;
    if (w->dock_slot != Dock_None) {
        const float gap = 10.0f * g.ui_scale;
        const float full_w = Max(g.io.display_size.x - gap * 2.0f, 160.0f);
        const float full_h = Max(g.io.display_size.y - gap * 2.0f, 80.0f);
        if (w->dock_slot == Dock_Left)   { w->pos = Vec2(gap, gap); w->size_full = Vec2(full_w * 0.32f, full_h); }
        if (w->dock_slot == Dock_Right)  { w->size_full = Vec2(full_w * 0.32f, full_h); w->pos = Vec2(g.io.display_size.x - gap - w->size_full.x, gap); }
        if (w->dock_slot == Dock_Top)    { w->pos = Vec2(gap, gap); w->size_full = Vec2(full_w, full_h * 0.34f); }
        if (w->dock_slot == Dock_Bottom) { w->size_full = Vec2(full_w, full_h * 0.34f); w->pos = Vec2(gap, g.io.display_size.y - gap - w->size_full.y); }
        if (w->dock_slot == Dock_Fill)   { w->pos = Vec2(gap, gap); w->size_full = Vec2(full_w, full_h); }
    }
    g.cur_window = w;
    w->active_this_frame = true;
    w->draw.Clear();
    w->draw.cur_tex = g.atlas.tex_id;
    w->draw.white_uv = g.atlas.white_uv;
    w->id_stack.clear();
    w->id_stack.push_back(w->id);

    if (g.nav_window == 0) g.nav_window = w;

    float title_h = s.window_title_height;

    // wheel scroll
    if (g.hovered_window == w && g.io.mouse_wheel != 0 && !w->collapsed)
        w->scroll_y -= g.io.mouse_wheel * 36.0f;
    w->scroll_y = Clamp(w->scroll_y, 0.0f, w->scroll_max_y);

    Vec2 pos = w->pos;
    Vec2 size = w->collapsed ? Vec2(w->size_full.x, title_h) : w->size_full;
    w->size = size;
    DrawList* dl = &w->draw;
    bool focused = (g.nav_window == w);

    dl->PushClipRect(Vec4(pos.x - s.shadow_size - 2, pos.y - s.shadow_size - 2,
                          pos.x + size.x + s.shadow_size + 2, pos.y + size.y + s.shadow_size + 2));

    dl->AddShadowRect(pos, Vec2(pos.x + size.x, pos.y + size.y),
                      GetColorU32(Col_WindowShadow), s.window_rounding, s.shadow_size);

    if (!w->collapsed)
        dl->AddRectFilledRounded(pos, Vec2(pos.x + size.x, pos.y + size.y), GetColorU32(Col_WindowBg), s.window_rounding);
    dl->AddRectFilledRounded(pos, Vec2(pos.x + size.x, pos.y + title_h + 8), GetColorU32(focused ? Col_TitleBgActive : Col_TitleBg), s.window_rounding);
    dl->AddRectFilled(Vec2(pos.x, pos.y + title_h), Vec2(pos.x + size.x, pos.y + title_h + 8), GetColorU32(Col_WindowBg));
    dl->AddRectFilledMultiColor(Vec2(pos.x + s.window_rounding, pos.y + title_h - 2),
                                Vec2(pos.x + size.x - s.window_rounding, pos.y + title_h),
                                GetColorU32(Col_GradientStart), GetColorU32(Col_GradientEnd),
                                GetColorU32(Col_GradientEnd), GetColorU32(Col_GradientStart));
    dl->AddCircleFilled(Vec2(pos.x + 18, pos.y + title_h * 0.5f), 4.0f, GetColorU32(Col_CheckMark), 16);

    // collapse arrow
    float asz = g.atlas.line_height * 0.7f;
    RenderArrow(dl, Vec2(pos.x + 30, pos.y + (title_h - asz) * 0.5f), asz, GetColorU32(Col_TextDisabled), w->collapsed ? 0 : 1);
    Vec4 arrow_rect(pos.x, pos.y, pos.x + 52, pos.y + title_h);

    // title text
    const char* disp_end = FindDisplayEnd(name);
    dl->AddText(Vec2(pos.x + title_h + 18, pos.y + s.frame_padding.y), GetColorU32(Col_Text), name, disp_end);

    // close button
    bool close_clicked = false;
    if (p_open) {
        Vec4 crect(pos.x + size.x - title_h, pos.y, pos.x + size.x, pos.y + title_h);
        bool chov = (g.hovered_window == w) && PointIn(g.io.mouse_pos, crect);
        if (chov) dl->AddRectFilled(Vec2(crect.x, crect.y), Vec2(crect.z, crect.w), GetColorU32(Col_ButtonHovered));
        Vec2 cc((crect.x + crect.z) * 0.5f, (crect.y + crect.w) * 0.5f);
        float rr = 3.5f;
        dl->AddLine(Vec2(cc.x - rr, cc.y - rr), Vec2(cc.x + rr, cc.y + rr), GetColorU32(Col_Text), 1.0f);
        dl->AddLine(Vec2(cc.x + rr, cc.y - rr), Vec2(cc.x - rr, cc.y + rr), GetColorU32(Col_Text), 1.0f);
        if (chov && g.mouse_clicked[0]) close_clicked = true;
    }

    dl->AddRect(pos, Vec2(pos.x + size.x, pos.y + size.y), GetColorU32(Col_Border), 1.0f);

    // -------- interactions --------
    bool title_hov = (g.hovered_window == w) && PointIn(g.io.mouse_pos, Vec4(pos.x, pos.y, pos.x + size.x, pos.y + title_h));
    bool arrow_hov = (g.hovered_window == w) && PointIn(g.io.mouse_pos, arrow_rect);

    if (g.hovered_window == w && g.mouse_clicked[0]) FocusWindow(w);

    if (arrow_hov && g.mouse_clicked[0]) {
        w->collapsed = !w->collapsed;
    } else if (title_hov && !close_clicked && g.mouse_clicked[0]) {
        w->dock_slot = Dock_None;
        g.moving_window = w; g.active_id = 0;
    }
    if (close_clicked && p_open) {
        *p_open = false;
        g.events.push_back(Event(Event_WindowClosed, w->id, name));
    }

    // -------- layout setup --------
    w->scrollbar_active = (!w->collapsed && w->scroll_max_y > 0.0f);
    float sb = w->scrollbar_active ? s.scrollbar_size : 0.0f;
    w->cursor_start = Vec2(pos.x + s.window_padding.x, pos.y + title_h + s.window_padding.y - w->scroll_y);
    w->cursor = w->cursor_start;
    w->cursor_prev_line = w->cursor;
    w->cursor_max = w->cursor_start;
    w->curr_line_height = w->prev_line_height = 0;
    w->indent = 0;
    w->content_w = size.x - s.window_padding.x * 2 - sb;

    if (!w->collapsed)
        dl->PushClipRect(Vec4(pos.x, pos.y + title_h, pos.x + size.x - sb, pos.y + size.y));
    else
        dl->PushClipRect(Vec4(pos.x, pos.y, pos.x, pos.y));

    return !w->collapsed;
}

void End() {
    Context& g = *GOG;
    Style& s = g.style;
    Window* w = g.cur_window;
    DrawList* dl = &w->draw;

    dl->PopClipRect();  // content clip

    // content height / scroll range
    float content_h = (w->cursor_max.y - w->cursor_start.y) + s.window_padding.y;
    float avail = w->size.y - s.window_title_height - s.window_padding.y;
    w->scroll_max_y = (content_h > avail) ? (content_h - avail) : 0.0f;

    if (!w->collapsed) {
        Vec2 pos = w->pos, size = w->size;

        // scrollbar
        if (w->scroll_max_y > 0.0f) {
            float track_x0 = pos.x + size.x - s.scrollbar_size;
            float track_y0 = pos.y + s.window_title_height;
            float track_y1 = pos.y + size.y;
            float track_h = track_y1 - track_y0;
            dl->AddRectFilled(Vec2(track_x0, track_y0), Vec2(pos.x + size.x, track_y1), GetColorU32(Col_ScrollbarBg));
            float grab_h = Max(20.0f, track_h * (avail / content_h));
            float t = w->scroll_y / w->scroll_max_y;
            float grab_y = track_y0 + t * (track_h - grab_h);
            dl->AddRectFilled(Vec2(track_x0 + 2, grab_y), Vec2(pos.x + size.x - 2, grab_y + grab_h), GetColorU32(Col_ScrollbarGrab));
        }

        // resize grip
        Vec4 grip(pos.x + size.x - 16, pos.y + size.y - 16, pos.x + size.x, pos.y + size.y);
        ID gid = w->id ^ 0x9E3779B97F4A7C15ULL;
        bool ghov = (g.hovered_window == w) && PointIn(g.io.mouse_pos, grip);
        bool held = false;
        if (ghov && g.mouse_clicked[0]) {
            w->dock_slot = Dock_None;
            g.active_id = gid; g.active_id_window = w;
            g.active_id_click_offset = Vec2(g.io.mouse_pos.x - (pos.x + size.x), g.io.mouse_pos.y - (pos.y + size.y));
            FocusWindow(w);
        }
        if (g.active_id == gid) {
            if (g.io.mouse_down[0]) {
                held = true;
                w->size_full.x = Max(160.0f, g.io.mouse_pos.x - pos.x - g.active_id_click_offset.x);
                w->size_full.y = Max(60.0f,  g.io.mouse_pos.y - pos.y - g.active_id_click_offset.y);
            } else g.active_id = 0;
        }
        U32 gc = GetColorU32(held ? Col_ResizeGripActive : (ghov ? Col_ResizeGripHovered : Col_ResizeGrip));
        dl->AddTriangleFilled(Vec2(grip.z, grip.w), Vec2(grip.z, grip.w - 14), Vec2(grip.z - 14, grip.w), gc);
    }

    dl->PopClipRect();  // window clip
    g.cur_window = 0;
}

// =====================================================================
//  Widgets
// =====================================================================
static void TextImpl(const char* str, const char* end, U32 col) {
    Window* w = GOG->cur_window;
    Vec2 pos = w->cursor;
    Vec2 sz = CalcTextSize(str, end);
    w->draw.AddText(pos, col, str, end);
    ItemSize(sz);
}
static void TextV(U32 col, const char* fmt, va_list args) {
    char buf[1024];
    vsnprintf(buf, sizeof(buf), fmt, args);
    TextImpl(buf, 0, col);
}
void Text(const char* fmt, ...)         { va_list a; va_start(a, fmt); TextV(GetColorU32(Col_Text), fmt, a); va_end(a); }
void TextDisabled(const char* fmt, ...) { va_list a; va_start(a, fmt); TextV(GetColorU32(Col_TextDisabled), fmt, a); va_end(a); }
void TextColored(const Vec4& c, const char* fmt, ...) {
    U32 col = OG_COL32((int)(c.x*255), (int)(c.y*255), (int)(c.z*255), (int)(c.w*255));
    va_list a; va_start(a, fmt); TextV(col, fmt, a); va_end(a);
}
void BulletText(const char* fmt, ...) {
    Window* w = GOG->cur_window;
    float lh = GOG->atlas.line_height;
    w->draw.AddCircleFilled(Vec2(w->cursor.x + lh * 0.25f, w->cursor.y + lh * 0.5f), lh * 0.18f, GetColorU32(Col_Text), 8);
    w->cursor.x += lh * 0.6f;
    char buf[1024]; va_list a; va_start(a, fmt); vsnprintf(buf, sizeof(buf), fmt, a); va_end(a);
    TextImpl(buf, 0, GetColorU32(Col_Text));
}

static bool ButtonImpl(const char* label, Vec2 size_arg) {
    Window* w = GOG->cur_window;
    Style& s = GOG->style;
    ID id = GetID(label);
    const char* end = FindDisplayEnd(label);
    Vec2 ls = CalcTextSize(label, end);
    Vec2 size((size_arg.x > 0 ? size_arg.x : ls.x + s.frame_padding.x * 2),
              (size_arg.y > 0 ? size_arg.y : ls.y + s.frame_padding.y * 2));
    Vec2 pos = w->cursor;
    Vec4 r(pos.x, pos.y, pos.x + size.x, pos.y + size.y);
    ItemSize(size);
    bool hovered, held;
    bool pressed = ButtonBehavior(r, id, &hovered, &held);
    float hover_t = AnimateID(id ^ 0xB0770A11ULL, hovered ? 1.0f : 0.0f, 18.0f);
    U32 col = held ? GetColorU32(Col_ButtonActive)
                   : ColorLerp(GetColorU32(Col_Button), GetColorU32(Col_ButtonHovered), hover_t);
    w->draw.AddRectFilledRounded(pos, Vec2(r.z, r.w), col, s.frame_rounding);
    if (hover_t > 0.01f)
        w->draw.AddLine(Vec2(pos.x + s.frame_rounding, pos.y + 1),
                        Vec2(r.z - s.frame_rounding, pos.y + 1),
                        ColorWithAlpha(OG_COL32_WHITE, (int)(45 * hover_t)), 1.0f);
    RenderTextClipped(r, label, end, true);
    if (pressed) GOG->events.push_back(Event(Event_Clicked, id, label));
    return pressed;
}
bool Button(const char* label, const Vec2& size) { return ButtonImpl(label, size); }
bool SmallButton(const char* label) {
    Style& s = GOG->style; Vec2 save = s.frame_padding; s.frame_padding.y = 0;
    bool r = ButtonImpl(label, Vec2(0, 0)); s.frame_padding = save; return r;
}

bool Checkbox(const char* label, bool* v) {
    Window* w = GOG->cur_window;
    Style& s = GOG->style;
    ID id = GetID(label);
    const char* end = FindDisplayEnd(label);
    float sq = GOG->atlas.line_height + s.frame_padding.y * 2;
    Vec2 ls = CalcTextSize(label, end);
    Vec2 pos = w->cursor;
    Vec2 total(sq + s.item_inner_spacing.x + ls.x, sq);
    Vec4 box(pos.x, pos.y, pos.x + sq, pos.y + sq);
    ItemSize(total);
    bool hovered, held;
    bool pressed = ButtonBehavior(Vec4(pos.x, pos.y, pos.x + total.x, pos.y + total.y), id, &hovered, &held);
    if (pressed) {
        *v = !*v;
        GOG->events.push_back(Event(Event_ValueChanged, id, label));
    }
    U32 bg = GetColorU32(held ? Col_FrameBgActive : (hovered ? Col_FrameBgHovered : Col_FrameBg));
    float pill_h = sq * 0.72f, pill_w = sq * 1.35f;
    Vec2 pill_a(box.x, box.y + (sq - pill_h) * 0.5f), pill_b(box.x + pill_w, box.y + (sq + pill_h) * 0.5f);
    float toggle_t = AnimateID(id ^ 0xC4EC0B11ULL, *v ? 1.0f : 0.0f, 20.0f);
    U32 track_col = ColorLerp(bg, GetColorU32(Col_Button), toggle_t);
    w->draw.AddRectFilledRounded(pill_a, pill_b, track_col, pill_h * 0.5f);
    float knob_x = pill_a.x + pill_h * 0.5f + (pill_w - pill_h) * toggle_t;
    w->draw.AddCircleFilled(Vec2(knob_x, (pill_a.y + pill_b.y) * 0.5f), pill_h * 0.34f,
                            ColorLerp(GetColorU32(Col_TextDisabled), OG_COL32_WHITE, toggle_t), 16);
    w->draw.AddText(Vec2(pos.x + pill_w + s.item_inner_spacing.x, pos.y + s.frame_padding.y), GetColorU32(Col_Text), label, end);
    return pressed;
}

bool RadioButton(const char* label, int* v, int v_button) {
    Window* w = GOG->cur_window;
    Style& s = GOG->style;
    ID id = GetID(label);
    const char* end = FindDisplayEnd(label);
    float sq = GOG->atlas.line_height + s.frame_padding.y * 2;
    Vec2 ls = CalcTextSize(label, end);
    Vec2 pos = w->cursor;
    Vec2 total(sq + s.item_inner_spacing.x + ls.x, sq);
    ItemSize(total);
    bool hovered, held;
    bool pressed = ButtonBehavior(Vec4(pos.x, pos.y, pos.x + total.x, pos.y + total.y), id, &hovered, &held);
    if (pressed) {
        *v = v_button;
        GOG->events.push_back(Event(Event_ValueChanged, id, label));
    }
    Vec2 c(pos.x + sq * 0.5f, pos.y + sq * 0.5f);
    U32 bg = GetColorU32(held ? Col_FrameBgActive : (hovered ? Col_FrameBgHovered : Col_FrameBg));
    w->draw.AddCircleFilled(c, sq * 0.5f, bg, 16);
    float selected_t = AnimateID(id ^ 0xA11D1010ULL, *v == v_button ? 1.0f : 0.0f, 18.0f);
    if (selected_t > 0.01f) w->draw.AddCircleFilled(c, sq * 0.25f * selected_t, GetColorU32(Col_CheckMark), 16);
    w->draw.AddText(Vec2(pos.x + sq + s.item_inner_spacing.x, pos.y + s.frame_padding.y), GetColorU32(Col_Text), label, end);
    return pressed;
}

static bool SliderBehavior(const Vec4& r, ID id, float* v, float v_min, float v_max, bool* out_held) {
    Context& g = *GOG;
    RegisterFocusable(id);
    bool hovered = ItemHoverable(r, id);
    bool changed = false, held = false;
    if (hovered && g.mouse_clicked[0]) {
        g.active_id = id; g.active_id_window = g.cur_window; g.nav_id = id;
        g.text_active_id = 0; FocusWindow(g.cur_window);
    }
    if (g.active_id == id) {
        if (g.io.mouse_down[0]) {
            held = true;
            float grab = GOG->style.grab_min_size;
            float usable = (r.z - r.x) - grab;
            float t = (usable > 0) ? Clamp((g.io.mouse_pos.x - r.x - grab * 0.5f) / usable, 0.0f, 1.0f) : 0.0f;
            float nv = v_min + t * (v_max - v_min);
            if (nv != *v) { *v = nv; changed = true; }
        } else g.active_id = 0;
    }
    if (g.nav_id == id) {
        float step = (v_max - v_min) / 100.0f;
        if (step <= 0.0f) step = 1.0f;
        if (g.key_pressed[37] || g.key_pressed[40]) { *v = Clamp(*v - step, v_min, v_max); changed = true; }
        if (g.key_pressed[38] || g.key_pressed[39]) { *v = Clamp(*v + step, v_min, v_max); changed = true; }
    }
    if (out_held) *out_held = held || (g.active_id == id);
    return changed;
}
static bool SliderScalar(const char* label, float* v, float v_min, float v_max, const char* fmt, bool is_int) {
    Window* w = GOG->cur_window;
    Style& s = GOG->style;
    ID id = GetID(label);
    const char* end = FindDisplayEnd(label);
    float fw = CalcItemWidth();
    float fh = GOG->atlas.line_height + s.frame_padding.y * 2;
    Vec2 pos = w->cursor;
    Vec4 frame(pos.x, pos.y, pos.x + fw, pos.y + fh);
    Vec2 ls = CalcTextSize(label, end);
    ItemSize(Vec2(fw + s.item_inner_spacing.x + ls.x, fh));
    bool held;
    bool changed = SliderBehavior(frame, id, v, v_min, v_max, &held);
    *v = Clamp(*v, v_min, v_max);
    // frame
    float track_y = (frame.y + frame.w) * 0.5f;
    w->draw.AddRectFilledRounded(Vec2(frame.x, track_y - 3), Vec2(frame.z, track_y + 3),
                                 GetColorU32(held ? Col_FrameBgActive : Col_FrameBg), 3.0f);
    // grab
    float grab = s.grab_min_size;
    float t = (v_max > v_min) ? (*v - v_min) / (v_max - v_min) : 0.0f;
    float gx = frame.x + t * ((frame.z - frame.x) - grab);
    w->draw.AddRectFilledRounded(Vec2(frame.x, track_y - 3), Vec2(gx + grab * 0.5f, track_y + 3),
                                 GetColorU32(held ? Col_SliderGrabActive : Col_SliderGrab), 3.0f);
    w->draw.AddCircleFilled(Vec2(gx + grab * 0.5f, track_y), 7.0f,
                            GetColorU32(held ? Col_SliderGrabActive : Col_SliderGrab), 18);
    // value text
    char buf[64];
    if (is_int) snprintf(buf, sizeof(buf), "%d", (int)(*v + (*v >= 0 ? 0.5f : -0.5f)));
    else        snprintf(buf, sizeof(buf), fmt, *v);
    RenderTextClipped(frame, buf, 0, true);
    // label
    w->draw.AddText(Vec2(frame.z + s.item_inner_spacing.x, pos.y + s.frame_padding.y), GetColorU32(Col_Text), label, end);
    if (changed) GOG->events.push_back(Event(Event_ValueChanged, id, label));
    return changed;
}
bool SliderFloat(const char* label, float* v, float v_min, float v_max, const char* fmt) {
    return SliderScalar(label, v, v_min, v_max, fmt, false);
}
bool SliderInt(const char* label, int* v, int v_min, int v_max) {
    float f = (float)*v;
    bool c = SliderScalar(label, &f, (float)v_min, (float)v_max, "%.0f", true);
    *v = (int)(f + (f >= 0 ? 0.5f : -0.5f));
    return c;
}

static int UTF8Prev(const char* text, int cursor) {
    if (cursor <= 0) return 0;
    int p = cursor - 1;
    while (p > 0 && (((unsigned char)text[p] & 0xC0) == 0x80)) --p;
    return p;
}
static int UTF8Next(const char* text, int length, int cursor) {
    if (cursor >= length) return length;
    int p = cursor + 1;
    while (p < length && (((unsigned char)text[p] & 0xC0) == 0x80)) ++p;
    return p;
}
static int EncodeUTF8(unsigned int cp, char out[5]) {
    if (cp <= 0x7F) { out[0] = (char)cp; out[1] = 0; return 1; }
    if (cp <= 0x7FF) {
        out[0] = (char)(0xC0 | (cp >> 6)); out[1] = (char)(0x80 | (cp & 0x3F)); out[2] = 0; return 2;
    }
    if (cp >= 0xD800 && cp <= 0xDFFF) cp = 0xFFFD;
    if (cp <= 0xFFFF) {
        out[0] = (char)(0xE0 | (cp >> 12)); out[1] = (char)(0x80 | ((cp >> 6) & 0x3F));
        out[2] = (char)(0x80 | (cp & 0x3F)); out[3] = 0; return 3;
    }
    if (cp > 0x10FFFF) cp = 0xFFFD;
    out[0] = (char)(0xF0 | (cp >> 18)); out[1] = (char)(0x80 | ((cp >> 12) & 0x3F));
    out[2] = (char)(0x80 | ((cp >> 6) & 0x3F)); out[3] = (char)(0x80 | (cp & 0x3F)); out[4] = 0;
    return 4;
}
static bool InsertBytes(char* buffer, int capacity, int& length, int& cursor, const char* bytes, int count) {
    if (!bytes || count <= 0 || length + count >= capacity) return false;
    memmove(buffer + cursor + count, buffer + cursor, (size_t)(length - cursor + 1));
    memcpy(buffer + cursor, bytes, (size_t)count);
    cursor += count; length += count;
    return true;
}
static int CursorFromPoint(const char* buffer, int length, const Vec2& point, const Vec2& text_pos, bool multiline) {
    float target_y = point.y - text_pos.y;
    int line = multiline ? (int)(target_y / Max(GOG->atlas.line_height, 1.0f)) : 0;
    if (line < 0) line = 0;
    int line_index = 0;
    int line_start = 0;
    for (int i = 0; i <= length; ++i) {
        if (i == length || buffer[i] == '\n') {
            if (line_index == line) {
                float x = text_pos.x;
                int cursor = line_start;
                while (cursor < i) {
                    int next = UTF8Next(buffer, i, cursor);
                    Vec2 sz = CalcTextSize(buffer + cursor, buffer + next);
                    if (point.x < x + sz.x * 0.5f) return cursor;
                    x += sz.x; cursor = next;
                }
                return i;
            }
            ++line_index; line_start = i + 1;
        }
    }
    return length;
}
static void CursorScreenPosition(const char* buffer, int cursor, const Vec2& text_pos, bool password, Vec2& out) {
    int line_start = 0, lines = 0;
    for (int i = 0; i < cursor; ++i) if (buffer[i] == '\n') { line_start = i + 1; ++lines; }
    if (password) {
        int count = 0;
        for (int i = line_start; i < cursor; i = UTF8Next(buffer, cursor, i)) ++count;
        std::string stars((size_t)count, '*');
        out = Vec2(text_pos.x + CalcTextSize(stars.c_str()).x, text_pos.y + lines * GOG->atlas.line_height);
    } else {
        out = Vec2(text_pos.x + CalcTextSize(buffer + line_start, buffer + cursor).x,
                   text_pos.y + lines * GOG->atlas.line_height);
    }
}
static bool InputTextEx(const char* label, char* buffer, int buffer_size, const Vec2& size_arg,
                        int flags, bool multiline) {
    if (!buffer || buffer_size <= 0 || !GOG->cur_window) return false;
    Window* w = GOG->cur_window;
    Context& g = *GOG;
    Style& s = g.style;
    ID id = GetID(label);
    RegisterFocusable(id);
    const char* label_end = FindDisplayEnd(label);
    Vec2 label_size = CalcTextSize(label, label_end);
    float width = size_arg.x > 0.0f ? size_arg.x : (size_arg.x < 0.0f ? Max(80.0f, w->content_w - (label_size.x > 0 ? label_size.x + s.item_inner_spacing.x : 0.0f)) : CalcItemWidth());
    float height = size_arg.y > 0.0f ? size_arg.y : g.atlas.line_height + s.frame_padding.y * 2.0f;
    Vec2 pos = w->cursor;
    Vec4 frame(pos.x, pos.y, pos.x + width, pos.y + height);
    Vec2 total(width + (label_size.x > 0 ? s.item_inner_spacing.x + label_size.x : 0.0f), height);
    ItemSize(total);
    bool hovered = ItemHoverable(frame, id);
    Vec2 text_pos(pos.x + s.frame_padding.x, pos.y + s.frame_padding.y);
    int length = (int)strlen(buffer);
    int& cursor = g.text_cursor[id];
    if (cursor < 0 || cursor > length) cursor = length;
    if (hovered && g.mouse_clicked[0]) {
        g.text_active_id = id; g.nav_id = id; FocusWindow(w);
        cursor = CursorFromPoint(buffer, length, g.io.mouse_pos, text_pos, multiline);
    }
    if (g.nav_activate_id == id) { g.text_active_id = id; cursor = length; }
    bool active = g.text_active_id == id;
    bool changed = false, submitted = false;
    bool read_only = (flags & InputTextFlags_ReadOnly) != 0;
    if (active) {
        if (g.key_pressed[27]) g.text_active_id = 0; // Escape
        if (g.key_pressed[37]) cursor = UTF8Prev(buffer, cursor);
        if (g.key_pressed[39]) cursor = UTF8Next(buffer, length, cursor);
        if (g.key_pressed[36]) cursor = 0;
        if (g.key_pressed[35]) cursor = length;
        bool ctrl = g.io.key_down[17];
        if (ctrl && g.key_pressed['C'] && g.io.set_clipboard_text)
            g.io.set_clipboard_text(g.io.clipboard_user_data, buffer);
        if (!read_only && ctrl && g.key_pressed['X'] && g.io.set_clipboard_text) {
            g.io.set_clipboard_text(g.io.clipboard_user_data, buffer);
            buffer[0] = 0; length = cursor = 0; changed = true;
        }
        if (!read_only && ctrl && g.key_pressed['V'] && g.io.get_clipboard_text) {
            const char* clip = g.io.get_clipboard_text(g.io.clipboard_user_data);
            if (clip) {
                for (const char* p = clip; *p; ) {
                    const char* begin = p;
                    unsigned int cp = DecodeUTF8(p, 0);
                    if ((!multiline && (cp == '\r' || cp == '\n')) || cp == '\r') continue;
                    if (InsertBytes(buffer, buffer_size, length, cursor, begin, (int)(p - begin))) changed = true;
                }
            }
        }
        if (!read_only && g.key_pressed[8] && cursor > 0) {
            int previous = UTF8Prev(buffer, cursor);
            memmove(buffer + previous, buffer + cursor, (size_t)(length - cursor + 1));
            length -= cursor - previous; cursor = previous; changed = true;
        }
        if (!read_only && g.key_pressed[46] && cursor < length) {
            int next = UTF8Next(buffer, length, cursor);
            memmove(buffer + cursor, buffer + next, (size_t)(length - next + 1));
            length -= next - cursor; changed = true;
        }
        if (g.key_pressed[13]) {
            if (multiline && !read_only) { const char nl = '\n'; changed |= InsertBytes(buffer, buffer_size, length, cursor, &nl, 1); }
            else { submitted = true; if (flags & InputTextFlags_EnterReturnsTrue) g.text_active_id = 0; }
        }
        if (!read_only) {
            for (int i = 0; i < g.io.input_char_count; ++i) {
                unsigned int cp = g.io.input_chars[i];
                if (cp < 32 || cp == 127) continue;
                char encoded[5]; int count = EncodeUTF8(cp, encoded);
                if (InsertBytes(buffer, buffer_size, length, cursor, encoded, count)) changed = true;
            }
        }
    }
    U32 bg = GetColorU32(active ? Col_FrameBgActive : (hovered ? Col_FrameBgHovered : Col_FrameBg));
    w->draw.AddRectFilledRounded(pos, Vec2(frame.z, frame.w), bg, s.frame_rounding);
    if (active || g.nav_id == id)
        w->draw.AddRect(pos, Vec2(frame.z, frame.w), ColorWithAlpha(GetColorU32(Col_ButtonHovered), active ? 230 : 120), 1.0f);
    w->draw.PushClipRect(Vec4(frame.x + 1, frame.y + 1, frame.z - 1, frame.w - 1));
    if (flags & InputTextFlags_Password) {
        int count = 0;
        for (int i = 0; i < length; i = UTF8Next(buffer, length, i)) if (buffer[i] != '\n') ++count;
        std::string stars((size_t)count, '*');
        w->draw.AddText(text_pos, GetColorU32(Col_Text), stars.c_str());
    } else w->draw.AddText(text_pos, GetColorU32(Col_Text), buffer);
    if (active && fmod(g.time, 1.0) < 0.55) {
        Vec2 caret; CursorScreenPosition(buffer, cursor, text_pos, (flags & InputTextFlags_Password) != 0, caret);
        w->draw.AddLine(caret, Vec2(caret.x, caret.y + g.atlas.line_height), GetColorU32(Col_Text), 1.0f);
    }
    w->draw.PopClipRect();
    if (label_size.x > 0)
        w->draw.AddText(Vec2(frame.z + s.item_inner_spacing.x, pos.y + s.frame_padding.y), GetColorU32(Col_Text), label, label_end);
    if (changed) g.events.push_back(Event(Event_TextChanged, id, label));
    return changed || submitted;
}
bool InputText(const char* label, char* buffer, int buffer_size, int flags) {
    return InputTextEx(label, buffer, buffer_size, Vec2(0, 0), flags, false);
}
bool InputTextMultiline(const char* label, char* buffer, int buffer_size, const Vec2& size, int flags) {
    return InputTextEx(label, buffer, buffer_size, size, flags, true);
}

bool Combo(const char* label, int* current_item, const char* const items[], int item_count) {
    if (!current_item || !items || item_count <= 0) return false;
    Window* w = GOG->cur_window;
    ID id = GetID(label);
    int& open = GOG->storage[id ^ 0xC04B0ULL];
    const char* preview = (*current_item >= 0 && *current_item < item_count) ? items[*current_item] : "Select...";
    char button_label[512];
    snprintf(button_label, sizeof(button_label), "%s   v##%s_combo", preview, label);
    bool changed = false;
    float start_x = w->cursor.x;
    if (Button(button_label, Vec2(CalcItemWidth(), 0))) open = !open;
    SameLine(); Text("%s", label);
    if (open) {
        Indent(8.0f);
        for (int i = 0; i < item_count; ++i) {
            char item_label[512]; snprintf(item_label, sizeof(item_label), "%s%s##%s_%d", i == *current_item ? "* " : "  ", items[i], label, i);
            if (Button(item_label, Vec2(CalcItemWidth() - 8.0f, 0))) {
                *current_item = i; open = 0; changed = true;
                GOG->events.push_back(Event(Event_ValueChanged, id, label));
            }
        }
        Unindent(8.0f);
    }
    (void)start_x;
    return changed;
}

struct TabBarState { ID id; ID selected; std::vector<std::string> labels; TabBarState() : id(0), selected(0) {} };
static std::vector<TabBarState> GTabBars;
static std::map<ID, ID> GTabSelections;
static std::map<ID, std::vector<std::string> > GTabLabels;
bool BeginTabBar(const char* id) {
    TabBarState state; state.id = GetID(id ? id : "tabs"); state.selected = GTabSelections[state.id];
    std::vector<std::string>& previous=GTabLabels[state.id];
    if(!state.selected&&!previous.empty()){state.selected=HashStr(previous[0].c_str(),0,state.id);GTabSelections[state.id]=state.selected;}
    for(size_t i=0;i<previous.size();++i){ID tab_id=HashStr(previous[i].c_str(),0,state.id);U32 old=GOG->style.colors[Col_Button];if(state.selected==tab_id)GOG->style.colors[Col_Button]=GOG->style.colors[Col_ButtonActive];char button[320];snprintf(button,sizeof(button),"%s##tab_%llu_%d",previous[i].c_str(),(unsigned long long)state.id,(int)i);if(Button(button))state.selected=tab_id;GOG->style.colors[Col_Button]=old;if(i+1<previous.size())SameLine();}
    if(!previous.empty())Separator();GTabSelections[state.id]=state.selected;
    GTabBars.push_back(state); return true;
}
bool BeginTabItem(const char* label, bool* p_open) {
    if (GTabBars.empty() || (p_open && !*p_open)) return false;
    TabBarState& bar = GTabBars.back();
    ID id = HashStr(label, 0, bar.id);
    bar.labels.push_back(label?label:"");
    if (!bar.selected) { bar.selected = id; GTabSelections[bar.id] = id; }
    return bar.selected == id;
}
void EndTabItem() {}
void EndTabBar() {
    if (GTabBars.empty()) return;
    TabBarState bar = GTabBars.back(); GTabBars.pop_back();
    GTabSelections[bar.id] = bar.selected;GTabLabels[bar.id]=bar.labels;
}

bool ColorEdit4(const char* label, float color[4]) {
    if (!color) return false;
    ID id = GetID(label);
    int& open = GOG->storage[id ^ 0xC0104ULL];
    U32 packed = OG_COL32((int)(Clamp(color[0],0,1)*255), (int)(Clamp(color[1],0,1)*255),
                          (int)(Clamp(color[2],0,1)*255), (int)(Clamp(color[3],0,1)*255));
    Window* w = GOG->cur_window;
    w->id_stack.push_back(id);
    Vec2 swatch = w->cursor;
    if (Button("##colour_swatch", Vec2(44, GOG->atlas.line_height + GOG->style.frame_padding.y * 2))) open = !open;
    w->draw.AddRectFilledRounded(Vec2(swatch.x + 4, swatch.y + 4), Vec2(swatch.x + 40, swatch.y + GOG->atlas.line_height + GOG->style.frame_padding.y * 2 - 4), packed, 4.0f);
    SameLine(); Text("%s", label);
    bool changed = false;
    if (open) {
        Indent(10.0f);
        changed |= SliderFloat("Red##color", &color[0], 0, 1, "%.2f");
        changed |= SliderFloat("Green##color", &color[1], 0, 1, "%.2f");
        changed |= SliderFloat("Blue##color", &color[2], 0, 1, "%.2f");
        changed |= SliderFloat("Alpha##color", &color[3], 0, 1, "%.2f");
        Unindent(10.0f);
    }
    if (changed) GOG->events.push_back(Event(Event_ValueChanged, id, label));
    if (w->id_stack.size() > 1) w->id_stack.pop_back();
    return changed;
}

bool CollapsingHeader(const char* label) {
    Window* w = GOG->cur_window;
    Style& s = GOG->style;
    ID id = GetID(label);
    const char* end = FindDisplayEnd(label);
    int& open = GOG->storage[id];
    if (GOG->frame_count <= 2 && open == 0) {} // default closed (0)
    float h = GOG->atlas.line_height + s.frame_padding.y * 2;
    Vec2 pos = w->cursor;
    Vec2 size(w->content_w, h);
    Vec4 r(pos.x, pos.y, pos.x + size.x, pos.y + size.y);
    ItemSize(size);
    bool hovered, held;
    bool pressed = ButtonBehavior(r, id, &hovered, &held);
    if (pressed) open = !open;
    U32 col = GetColorU32(held ? Col_HeaderActive : (hovered ? Col_HeaderHovered : Col_Header));
    w->draw.AddRectFilled(pos, Vec2(r.z, r.w), col);
    float asz = GOG->atlas.line_height * 0.7f;
    RenderArrow(&w->draw, Vec2(pos.x + 4, pos.y + (h - asz) * 0.5f), asz, GetColorU32(Col_Text), open ? 1 : 0);
    w->draw.AddText(Vec2(pos.x + h, pos.y + s.frame_padding.y), GetColorU32(Col_Text), label, end);
    return open != 0;
}

bool TreeNode(const char* label) {
    Window* w = GOG->cur_window;
    ID id = GetID(label);
    const char* end = FindDisplayEnd(label);
    int& open = GOG->storage[id];
    float h = GOG->atlas.line_height;
    Vec2 pos = w->cursor;
    Vec2 size(w->content_w - w->indent, h);
    Vec4 r(pos.x, pos.y, pos.x + size.x, pos.y + size.y);
    ItemSize(size);
    bool hovered, held;
    bool pressed = ButtonBehavior(r, id, &hovered, &held);
    if (pressed) open = !open;
    if (hovered) w->draw.AddRectFilled(pos, Vec2(r.z, r.w), GetColorU32(Col_HeaderHovered));
    float asz = h * 0.7f;
    RenderArrow(&w->draw, Vec2(pos.x + 2, pos.y + (h - asz) * 0.5f), asz, GetColorU32(Col_Text), open ? 1 : 0);
    w->draw.AddText(Vec2(pos.x + h, pos.y), GetColorU32(Col_Text), label, end);
    if (open) Indent();
    return open != 0;
}
void TreePop() { Unindent(); }

void Separator() {
    Window* w = GOG->cur_window;
    Style& s = GOG->style;
    float y = w->cursor.y + 1;
    w->draw.AddLine(Vec2(w->cursor_start.x, y), Vec2(w->cursor_start.x + w->content_w, y), GetColorU32(Col_Separator), 1.0f);
    ItemSize(Vec2(w->content_w, s.item_spacing.y));
}

void ProgressBar(float fraction, const Vec2& size_arg, const char* overlay) {
    Window* w = GOG->cur_window;
    Style& s = GOG->style;
    fraction = Clamp(fraction, 0.0f, 1.0f);
    float fw = (size_arg.x > 0 ? size_arg.x : w->content_w * 0.62f);
    float fh = (size_arg.y > 0 ? size_arg.y : GOG->atlas.line_height + s.frame_padding.y * 2);
    Vec2 pos = w->cursor;
    Vec4 r(pos.x, pos.y, pos.x + fw, pos.y + fh);
    ItemSize(Vec2(fw, fh));
    w->draw.AddRectFilledRounded(pos, Vec2(r.z, r.w), GetColorU32(Col_FrameBg), s.frame_rounding);
    if (fraction > 0.0f) {
        float fill_x = pos.x + fw * fraction;
        w->draw.AddRectFilledRounded(pos, Vec2(fill_x, r.w), GetColorU32(Col_Button), s.frame_rounding);
        if (fill_x - pos.x > s.frame_rounding * 2)
            w->draw.AddRectFilledMultiColor(Vec2(pos.x + s.frame_rounding, pos.y), Vec2(fill_x - s.frame_rounding, r.w),
                                            GetColorU32(Col_GradientStart), GetColorU32(Col_GradientEnd),
                                            GetColorU32(Col_GradientEnd), GetColorU32(Col_GradientStart));
    }
    char buf[32];
    if (!overlay) { snprintf(buf, sizeof(buf), "%.0f%%", fraction * 100.0f); overlay = buf; }
    RenderTextClipped(r, overlay, 0, true);
}

void GlassCard(const char* label, const Vec2& size_arg, float blur_radius) {
    Window* w = GOG->cur_window;
    Style& s = GOG->style;
    float width = size_arg.x > 0.0f ? size_arg.x : GetContentRegionAvail().x;
    float height = size_arg.y > 0.0f ? size_arg.y : 72.0f;
    Vec2 pos = w->cursor;
    ItemSize(Vec2(width, height));
    U32 tint = ColorWithAlpha(GetColorU32(Col_TitleBgActive), 188);
    w->draw.AddShadowRect(pos, Vec2(pos.x + width, pos.y + height),
                          GetColorU32(Col_WindowShadow), s.frame_rounding + 4.0f, 8.0f);
    w->draw.AddBackdropBlur(pos, Vec2(pos.x + width, pos.y + height), tint,
                            blur_radius, s.frame_rounding + 4.0f);
    w->draw.AddRect(pos, Vec2(pos.x + width, pos.y + height), ColorWithAlpha(GetColorU32(Col_Border), 120), 1.0f);
    w->draw.AddCircleFilled(Vec2(pos.x + 18.0f, pos.y + height * 0.5f), 5.0f, GetColorU32(Col_Success), 16);
    w->draw.AddText(Vec2(pos.x + 32.0f, pos.y + (height - GOG->atlas.line_height) * 0.5f),
                    GetColorU32(Col_Text), label ? label : "Glass panel");
}

void Image(unsigned int texture_id, const Vec2& size, const Vec2& uv0, const Vec2& uv1, U32 tint) {
    if (!GOG->cur_window || size.x <= 0 || size.y <= 0) return;
    Window* w = GOG->cur_window; Vec2 pos = w->cursor; ItemSize(size);
    unsigned int previous = w->draw.cur_tex;
    w->draw.cur_tex = texture_id; NewCmd(&w->draw);
    DrawIdx base = (DrawIdx)w->draw.vtx.size(); w->draw.PrimReserve(6, 4);
    DrawVert v; v.col = tint;
    v.pos = pos; v.uv = uv0; w->draw.vtx.push_back(v);
    v.pos = Vec2(pos.x + size.x, pos.y); v.uv = Vec2(uv1.x, uv0.y); w->draw.vtx.push_back(v);
    v.pos = Vec2(pos.x + size.x, pos.y + size.y); v.uv = uv1; w->draw.vtx.push_back(v);
    v.pos = Vec2(pos.x, pos.y + size.y); v.uv = Vec2(uv0.x, uv1.y); w->draw.vtx.push_back(v);
    w->draw.idx.push_back(base); w->draw.idx.push_back(base + 1); w->draw.idx.push_back(base + 2);
    w->draw.idx.push_back(base); w->draw.idx.push_back(base + 2); w->draw.idx.push_back(base + 3);
    w->draw.cur_tex = previous; NewCmd(&w->draw);
}

void SetTooltip(const char* text) {
    if (!text || !*text) return;
    DrawList& dl = GOG->overlay_draw; Vec2 sz = CalcTextSize(text);
    Vec2 p(GOG->io.mouse_pos.x + 16.0f, GOG->io.mouse_pos.y + 18.0f);
    if (p.x + sz.x + 20 > GOG->io.display_size.x) p.x = GOG->io.display_size.x - sz.x - 20;
    if (p.y + sz.y + 14 > GOG->io.display_size.y) p.y = GOG->io.display_size.y - sz.y - 14;
    dl.AddShadowRect(p, Vec2(p.x + sz.x + 20, p.y + sz.y + 14), GetColorU32(Col_WindowShadow), 7, 7);
    dl.AddRectFilledRounded(p, Vec2(p.x + sz.x + 20, p.y + sz.y + 14), GetColorU32(Col_TitleBgActive), 7);
    dl.AddText(Vec2(p.x + 10, p.y + 7), GetColorU32(Col_Text), text);
}

struct ToastData { std::string message; ToastType type; double expires; };
static std::vector<ToastData> GToasts;
void AddToast(const char* message, ToastType type, float duration) {
    if (!message || !*message) return;
    ToastData toast; toast.message = message; toast.type = type; toast.expires = GOG->time + Max(duration, 0.25f); GToasts.push_back(toast);
}
void RenderNotifications() {
    DrawList& dl = GOG->overlay_draw;
    float y = 18.0f;
    for (size_t i = 0; i < GToasts.size();) {
        if (GToasts[i].expires <= GOG->time) { GToasts.erase(GToasts.begin() + (ptrdiff_t)i); continue; }
        ToastData& toast = GToasts[i]; Vec2 ts = CalcTextSize(toast.message.c_str());
        float width = Max(ts.x + 48.0f, 220.0f); float x = GOG->io.display_size.x - width - 18.0f;
        U32 accent = toast.type == Toast_Success ? GetColorU32(Col_Success) : toast.type == Toast_Warning ? GetColorU32(Col_Warning) : toast.type == Toast_Error ? OG_COL32(238,82,106,255) : GetColorU32(Col_Link);
        dl.AddShadowRect(Vec2(x,y), Vec2(x+width,y+46), GetColorU32(Col_WindowShadow), 9, 8);
        dl.AddBackdropBlur(Vec2(x,y), Vec2(x+width,y+46), ColorWithAlpha(GetColorU32(Col_TitleBgActive), 225), 6, 9);
        dl.AddRectFilledRounded(Vec2(x,y), Vec2(x+4,y+46), accent, 2);
        dl.AddText(Vec2(x+18,y+14), GetColorU32(Col_Text), toast.message.c_str());
        y += 56.0f; ++i;
    }
}

static std::map<ID, bool> GPopupOpen;
static std::vector<Window*> GPopupParents;
static std::vector<ID> GPopupIDs;
void OpenPopup(const char* id) { if (GOG->cur_window) GPopupOpen[GetID(id)] = true; }
bool BeginPopup(const char* id) {
    if (!GOG->cur_window) return false;
    ID popup_id = GetID(id); if (!GPopupOpen[popup_id]) return false;
    Window* parent = GOG->cur_window; Vec2 at(GOG->io.mouse_pos.x + 8, GOG->io.mouse_pos.y + 8);
    char name[128]; snprintf(name, sizeof(name), "%s##popup_%llu", id, (unsigned long long)popup_id);
    SetNextWindowPos(at); SetNextWindowSize(Vec2(300, 190));
    GPopupParents.push_back(parent); GPopupIDs.push_back(popup_id);
    bool visible = Begin(name, 0);
    if (GOG->key_pressed[27]) GPopupOpen[popup_id] = false;
    return visible;
}
bool BeginPopupModal(const char* title, bool* p_open) {
    if (p_open && !*p_open) return false;
    return BeginPopup(title);
}
void CloseCurrentPopup() { if (!GPopupIDs.empty()) GPopupOpen[GPopupIDs.back()] = false; }
void EndPopup() {
    if (GPopupParents.empty()) return;
    End(); GOG->cur_window = GPopupParents.back(); GPopupParents.pop_back(); GPopupIDs.pop_back();
}

struct TableState {
    Window* window; ID id; int columns, column, row, flags; float width, column_width, row_y, row_height;
    Vec2 outer_cursor, outer_start; float outer_content, outer_indent; TableSortSpec sort;
    TableState() : window(0), id(0), columns(0), column(0), row(0), flags(0), width(0), column_width(0), row_y(0), row_height(0) {}
};
static std::vector<TableState> GTables;
static std::map<ID, TableSortSpec> GTableSorts;
bool BeginTable(const char* id, int columns, int flags) {
    if (!GOG->cur_window || columns < 1) return false;
    Window* w = GOG->cur_window; TableState t; t.window=w; t.id=GetID(id); t.columns=columns; t.flags=flags;
    t.width=GetContentRegionAvail().x; t.column_width=t.width/columns; t.outer_cursor=w->cursor; t.outer_start=w->cursor_start;
    t.outer_content=w->content_w; t.outer_indent=w->indent; t.row_y=w->cursor.y; t.sort=GTableSorts[t.id];t.sort.dirty=false;GTableSorts[t.id].dirty=false; GTables.push_back(t);
    w->id_stack.push_back(t.id); w->content_w=t.column_width; w->cursor_start=w->cursor; w->indent=0; return true;
}
static void TableSetColumn(TableState& t) {
    Window* w=t.window; float x=t.outer_cursor.x+t.column*t.column_width;
    w->cursor_start=Vec2(x,t.row_y); w->cursor=w->cursor_start; w->cursor_prev_line=w->cursor; w->content_w=t.column_width; w->indent=0;
}
bool TableHeader(const char* label) {
    if (GTables.empty()) return false; TableState& t=GTables.back(); int col=t.column;
    char text[256]; const char* marker=(t.sort.column==col?(t.sort.direction==Sort_Ascending?"  ^":t.sort.direction==Sort_Descending?"  v":""):"");
    snprintf(text,sizeof(text),"%s%s##header_%d",label,marker,col); bool clicked=Button(text,Vec2(t.column_width-2,0));
    if (clicked && (t.flags&TableFlags_Sortable)) { t.sort.column=col; t.sort.direction=t.sort.direction==Sort_Ascending?Sort_Descending:Sort_Ascending; t.sort.dirty=true; GTableSorts[t.id]=t.sort; }
    TableNextColumn(); return clicked;
}
void TableNextColumn() {
    if (GTables.empty()) return; TableState& t=GTables.back(); Window* w=t.window;
    t.row_height=Max(t.row_height,Max(w->prev_line_height,GOG->atlas.line_height+GOG->style.frame_padding.y*2));
    ++t.column; if(t.column>=t.columns){t.column=0;++t.row;t.row_y+=t.row_height+2;t.row_height=0;} TableSetColumn(t);
}
void TableNextRow() {
    if(GTables.empty())return; TableState& t=GTables.back(); if(t.column!=0){t.column=0;++t.row;t.row_y+=Max(t.row_height,GOG->atlas.line_height+8)+2;t.row_height=0;} TableSetColumn(t);
}
bool TableSelectable(const char* label, bool selected) {
    if(GTables.empty())return false; TableState& t=GTables.back(); U32 old=GOG->style.colors[Col_Button];
    if(selected)GOG->style.colors[Col_Button]=GOG->style.colors[Col_HeaderActive];
    bool clicked=Button(label,Vec2(t.column_width-2,0)); GOG->style.colors[Col_Button]=old; TableNextColumn(); return clicked;
}
const TableSortSpec* TableGetSortSpec(){return GTables.empty()?0:&GTables.back().sort;}
void EndTable() {
    if(GTables.empty())return; TableState t=GTables.back();GTables.pop_back();Window* w=t.window;
    float total=(t.row_y-t.outer_cursor.y)+Max(t.row_height,GOG->atlas.line_height+8);
    if(w->id_stack.size()>1)w->id_stack.pop_back();w->cursor_start=t.outer_start;w->content_w=t.outer_content;w->indent=t.outer_indent;w->cursor=t.outer_cursor;ItemSize(Vec2(t.width,total));
}

static void PlotImpl(const char* label, const float* values, int count, Vec2 size_arg, bool histogram) {
    Window* w = GOG->cur_window;
    Style& s = GOG->style;
    const char* end = FindDisplayEnd(label);
    float fw = (size_arg.x > 0 ? size_arg.x : w->content_w * 0.62f);
    float fh = (size_arg.y > 0 ? size_arg.y : 60.0f);
    Vec2 pos = w->cursor;
    Vec4 r(pos.x, pos.y, pos.x + fw, pos.y + fh);
    Vec2 ls = CalcTextSize(label, end);
    ItemSize(Vec2(fw + s.item_inner_spacing.x + ls.x, fh));
    w->draw.AddRectFilled(pos, Vec2(r.z, r.w), GetColorU32(Col_FrameBg));
    if (count > 0) {
        float vmin = values[0], vmax = values[0];
        for (int i = 1; i < count; i++) { if (values[i] < vmin) vmin = values[i]; if (values[i] > vmax) vmax = values[i]; }
        if (vmax <= vmin) vmax = vmin + 1.0f;
        float inner = fh - 4;
        if (histogram) {
            U32 col = GetColorU32(Col_PlotHistogram);
            float bw = fw / count;
            for (int i = 0; i < count; i++) {
                float t = (values[i] - vmin) / (vmax - vmin);
                float bx = r.x + i * bw;
                w->draw.AddRectFilled(Vec2(bx + 1, r.w - 2 - t * inner), Vec2(bx + bw - 1, r.w - 2), col);
            }
        } else {
            U32 col = GetColorU32(Col_PlotLines);
            for (int i = 0; i < count - 1; i++) {
                float t0 = (values[i] - vmin) / (vmax - vmin);
                float t1 = (values[i + 1] - vmin) / (vmax - vmin);
                Vec2 p0(r.x + (float)i / (count - 1) * fw, r.w - 2 - t0 * inner);
                Vec2 p1(r.x + (float)(i + 1) / (count - 1) * fw, r.w - 2 - t1 * inner);
                w->draw.AddLine(p0, p1, col, 1.0f);
            }
        }
    }
    w->draw.AddText(Vec2(r.z + s.item_inner_spacing.x, pos.y + (fh - GOG->atlas.line_height) * 0.5f), GetColorU32(Col_Text), label, end);
}
void PlotLines(const char* label, const float* values, int count, const Vec2& size)     { PlotImpl(label, values, count, size, false); }
void PlotHistogram(const char* label, const float* values, int count, const Vec2& size)  { PlotImpl(label, values, count, size, true); }

// =====================================================================
//  Chart builder
// =====================================================================
struct ChartSeriesData {
    std::string label;
    std::vector<float> values;
    std::vector<Vec2> points;
    std::vector<Candlestick> candles;
    std::vector<std::string> slice_labels;
    U32 color;
    int type; // 0 line, 1 bars, 2 area, 3 scatter, 4 pie, 5 candlesticks
};
struct ChartBuildState {
    bool active;
    Window* window;
    ID id;
    std::string label;
    Vec4 rect;
    std::vector<ChartSeriesData> series;
    ChartBuildState() : active(false), window(0), id(0) {}
};
static ChartBuildState GChart;

bool BeginChart(const char* label, const Vec2& size_arg) {
    if (!GOG || !GOG->cur_window || GChart.active) return false;
    Window* w = GOG->cur_window;
    float width = size_arg.x > 0.0f ? size_arg.x : GetContentRegionAvail().x;
    float height = size_arg.y > 0.0f ? size_arg.y : 150.0f;
    Vec2 pos = w->cursor;
    GChart.active = true;
    GChart.window = w;
    GChart.id = GetID(label);
    GChart.label = label ? label : "Chart";
    GChart.rect = Vec4(pos.x, pos.y, pos.x + width, pos.y + height);
    GChart.series.clear();
    ItemSize(Vec2(width, height));
    return true;
}
static void ChartAddSeries(const char* label, const float* values, int count, U32 color, bool bars) {
    if (!GChart.active || !values || count <= 0) return;
    ChartSeriesData data;
    data.label = label ? label : "Series";
    data.values.assign(values, values + count);
    data.color = color;
    data.type = bars ? 1 : 0;
    GChart.series.push_back(data);
}
void ChartLine(const char* label, const float* values, int count, U32 color) {
    ChartAddSeries(label, values, count, color, false);
}
void ChartLine(const char* label, const StreamingSeries& series, U32 color) {
    if (!GChart.active || series.Size() <= 0) return;
    ChartSeriesData data;
    data.label = label ? label : "Series";
    series.GetOrdered(data.values);
    data.color = color;
    data.type = 0;
    GChart.series.push_back(data);
}
void ChartBars(const char* label, const float* values, int count, U32 color) {
    ChartAddSeries(label, values, count, color, true);
}
void ChartArea(const char* label, const float* values, int count, U32 color) {
    ChartAddSeries(label, values, count, color, false); if (!GChart.series.empty()) GChart.series.back().type = 2;
}
void ChartScatter(const char* label, const Vec2* points, int count, U32 color) {
    if (!GChart.active || !points || count <= 0) return; ChartSeriesData d; d.label=label?label:"Scatter"; d.points.assign(points,points+count); d.color=color; d.type=3; GChart.series.push_back(d);
}
void ChartPie(const char* label, const float* values, const char* const labels[], int count) {
    if (!GChart.active || !values || count<=0)return;ChartSeriesData d;d.label=label?label:"Pie";d.values.assign(values,values+count);d.color=0;d.type=4;
    for(int i=0;i<count;++i)d.slice_labels.push_back(labels&&labels[i]?labels[i]:"");GChart.series.push_back(d);
}
void ChartCandlesticks(const char* label, const Candlestick* values, int count) {
    if(!GChart.active||!values||count<=0)return;ChartSeriesData d;d.label=label?label:"OHLC";d.candles.assign(values,values+count);d.color=0;d.type=5;GChart.series.push_back(d);
}
void EndChart() {
    if (!GChart.active || !GChart.window) return;
    Window* w = GChart.window;
    const Style& s = GOG->style;
    Vec4 r = GChart.rect;
    w->draw.AddRectFilledRounded(Vec2(r.x, r.y), Vec2(r.z, r.w), GetColorU32(Col_FrameBg), s.frame_rounding + 2.0f);
    w->draw.AddText(Vec2(r.x + 12, r.y + 9), GetColorU32(Col_Text), GChart.label.c_str());

    Vec4 plot(r.x + 12, r.y + 34, r.z - 12, r.w - 12);
    for (int i = 1; i < 4; ++i) {
        float y = plot.y + (plot.w - plot.y) * (float)i / 4.0f;
        w->draw.AddLine(Vec2(plot.x, y), Vec2(plot.z, y), ColorWithAlpha(GetColorU32(Col_Separator), 75), 1.0f);
    }

    float vmin = 0.0f, vmax = 1.0f;
    bool first = true;
    for (size_t n = 0; n < GChart.series.size(); ++n) {
        const ChartSeriesData& series = GChart.series[n];
        const std::vector<float>& values = series.values;
        for (size_t i = 0; i < values.size(); ++i) {
            if (first) { vmin = vmax = values[i]; first = false; }
            else { vmin = Min(vmin, values[i]); vmax = Max(vmax, values[i]); }
        }
        for(size_t i=0;i<series.points.size();++i){float v=series.points[i].y;if(first){vmin=vmax=v;first=false;}else{vmin=Min(vmin,v);vmax=Max(vmax,v);}}
        for(size_t i=0;i<series.candles.size();++i){float lo=series.candles[i].low,hi=series.candles[i].high;if(first){vmin=lo;vmax=hi;first=false;}else{vmin=Min(vmin,lo);vmax=Max(vmax,hi);}}
    }
    if (vmax <= vmin) vmax = vmin + 1.0f;
    // Keep zero visible for positive-only or negative-only data.
    if (vmin > 0.0f) vmin = 0.0f;
    if (vmax < 0.0f) vmax = 0.0f;

    const U32 palette[] = {
        GetColorU32(Col_PlotLines), GetColorU32(Col_GradientStart),
        GetColorU32(Col_Warning), GetColorU32(Col_Link)
    };
    for (size_t n = 0; n < GChart.series.size(); ++n) {
        const ChartSeriesData& data = GChart.series[n];
        int count = (int)data.values.size();
        U32 color = data.color ? data.color : palette[n % 4];
        if (data.type == 4 && count > 0) {
            float sum=0;for(int i=0;i<count;++i)sum+=Max(data.values[(size_t)i],0.0f);if(sum<=0)sum=1;
            Vec2 center((plot.x+plot.z)*0.5f,(plot.y+plot.w)*0.5f);float radius=Min(plot.z-plot.x,plot.w-plot.y)*0.38f;float angle=-1.5707963f;
            for(int i=0;i<count;++i){float sweep=Max(data.values[(size_t)i],0.0f)/sum*6.2831853f;U32 slice=palette[i%4];int segs=(int)(sweep*14.0f);if(segs<2)segs=2;for(int q=0;q<segs;++q){float a0=angle+sweep*(float)q/(float)segs,a1=angle+sweep*(float)(q+1)/(float)segs;w->draw.AddTriangleFilled(center,Vec2(center.x+cosf(a0)*radius,center.y+sinf(a0)*radius),Vec2(center.x+cosf(a1)*radius,center.y+sinf(a1)*radius),slice);}angle+=sweep;}
        } else if (data.type == 5 && !data.candles.empty()) {
            float bw=(plot.z-plot.x)/data.candles.size();for(size_t i=0;i<data.candles.size();++i){const Candlestick& c=data.candles[i];float x=plot.x+(i+0.5f)*bw;float yh=plot.w-(c.high-vmin)/(vmax-vmin)*(plot.w-plot.y);float yl=plot.w-(c.low-vmin)/(vmax-vmin)*(plot.w-plot.y);float yo=plot.w-(c.open-vmin)/(vmax-vmin)*(plot.w-plot.y);float yc=plot.w-(c.close-vmin)/(vmax-vmin)*(plot.w-plot.y);U32 cc=c.close>=c.open?GetColorU32(Col_Success):OG_COL32(238,82,106,255);w->draw.AddLine(Vec2(x,yh),Vec2(x,yl),cc,1);w->draw.AddRectFilledRounded(Vec2(x-bw*0.28f,Min(yo,yc)),Vec2(x+bw*0.28f,Max(yo,yc)+1),cc,2);}
        } else if (data.type == 3 && !data.points.empty()) {
            float xmin=data.points[0].x,xmax=xmin;for(size_t i=1;i<data.points.size();++i){xmin=Min(xmin,data.points[i].x);xmax=Max(xmax,data.points[i].x);}if(xmax<=xmin)xmax=xmin+1;
            for(size_t i=0;i<data.points.size();++i){float x=plot.x+(data.points[i].x-xmin)/(xmax-xmin)*(plot.z-plot.x);float y=plot.w-(data.points[i].y-vmin)/(vmax-vmin)*(plot.w-plot.y);w->draw.AddCircleFilled(Vec2(x,y),3.5f,color,12);}
        } else if (data.type == 1 && count > 0) {
            float bw = (plot.z - plot.x) / Max((float)count, 1.0f);
            for (int i = 0; i < count; ++i) {
                float t = (data.values[(size_t)i] - vmin) / (vmax - vmin);
                float x0 = plot.x + i * bw;
                w->draw.AddRectFilledRounded(Vec2(x0 + 1, plot.w - t * (plot.w - plot.y)),
                                             Vec2(x0 + bw - 1, plot.w), ColorWithAlpha(color, 210), 2.0f);
            }
        } else if (count == 1) {
            float t = (data.values[0] - vmin) / (vmax - vmin);
            w->draw.AddCircleFilled(Vec2(plot.x, plot.w - t * (plot.w - plot.y)), 3.0f, color, 14);
        } else {
            int max_points = (int)(plot.z - plot.x);
            if (max_points < 2) max_points = 2;
            int step = count / max_points;
            if (step < 1) step = 1;
            Vec2 previous;
            bool have_previous = false;
            for (int i = 0; i < count; i += step) {
                int end = i + step; if (end > count) end = count;
                float sum = 0.0f;
                for (int j = i; j < end; ++j) sum += data.values[(size_t)j];
                float value = sum / (end - i);
                float x = plot.x + (float)i / (count - 1) * (plot.z - plot.x);
                float t = (value - vmin) / (vmax - vmin);
                Vec2 p(x, plot.w - t * (plot.w - plot.y));
                if (have_previous) {
                    w->draw.AddQuad(previous, p, Vec2(p.x, plot.w), Vec2(previous.x, plot.w), ColorWithAlpha(color, data.type == 2 ? 70 : 24));
                    w->draw.AddLine(previous, p, color, 2.0f);
                }
                previous = p;
                have_previous = true;
            }
        }
    }

    float legend_x = r.z - 12.0f;
    for (int n = (int)GChart.series.size() - 1; n >= 0; --n) {
        const ChartSeriesData& data = GChart.series[(size_t)n];
        Vec2 text_size = CalcTextSize(data.label.c_str());
        legend_x -= text_size.x;
        U32 color = data.color ? data.color : palette[n % 4];
        w->draw.AddText(Vec2(legend_x, r.y + 9), GetColorU32(Col_TextDisabled), data.label.c_str());
        legend_x -= 12.0f;
        w->draw.AddCircleFilled(Vec2(legend_x + 4, r.y + 16), 3.0f, color, 12);
        legend_x -= 10.0f;
    }

    GChart.active = false;
    GChart.window = 0;
    GChart.series.clear();
}

// =====================================================================
//  Markdown
// =====================================================================
static MarkdownLinkCallback GMarkdownLinkCallback = 0;
static MarkdownImageResolver GMarkdownImageResolver = 0;
static void* GMarkdownLinkUserData = 0;
static void* GMarkdownImageUserData = 0;
void SetMarkdownLinkCallback(MarkdownLinkCallback callback, void* user_data) { GMarkdownLinkCallback=callback; GMarkdownLinkUserData=user_data; }
void SetMarkdownImageResolver(MarkdownImageResolver resolver, void* user_data) { GMarkdownImageResolver=resolver; GMarkdownImageUserData=user_data; }

static bool MarkdownInteractiveLine(const std::string& line) {
    size_t open=line.find('['); if(open==std::string::npos)return false;
    bool image=open>0&&line[open-1]=='!'; size_t close=line.find(']',open+1);
    if(close==std::string::npos||close+1>=line.size()||line[close+1]!='(')return false;
    size_t finish=line.find(')',close+2);if(finish==std::string::npos)return false;
    std::string caption=line.substr(open+1,close-open-1),url=line.substr(close+2,finish-close-2);
    if(image){
        unsigned int tex=0;Vec2 size(0,0);
        if(GMarkdownImageResolver&&GMarkdownImageResolver(url.c_str(),&tex,&size,GMarkdownImageUserData)&&tex&&size.x>0&&size.y>0){Image(tex,size);return true;}
        GlassCard((std::string("Image: ")+caption).c_str(),Vec2(-1,64),4);return true;
    }
    Window* w=GOG->cur_window;Vec2 pos=w->cursor;float x=pos.x;
    if(open>0){std::string prefix=line.substr(0,open);w->draw.AddText(Vec2(x,pos.y),GetColorU32(Col_Text),prefix.c_str());x+=CalcTextSize(prefix.c_str()).x;}
    Vec2 link_size=CalcTextSize(caption.c_str());ID id=HashStr(url.c_str(),0,w->id_stack.back());Vec4 r(x,pos.y,x+link_size.x,pos.y+link_size.y);
    bool hovered=false,held=false,pressed=ButtonBehavior(r,id,&hovered,&held);w->draw.AddText(Vec2(x,pos.y),GetColorU32(Col_Link),caption.c_str());
    w->draw.AddLine(Vec2(x,r.w),Vec2(r.z,r.w),ColorWithAlpha(GetColorU32(Col_Link),hovered?255:120),1);
    if(pressed){GOG->events.push_back(Event(Event_LinkActivated,id,url.c_str()));if(GMarkdownLinkCallback)GMarkdownLinkCallback(url.c_str(),GMarkdownLinkUserData);}
    x=r.z;if(finish+1<line.size()){std::string suffix=line.substr(finish+1);w->draw.AddText(Vec2(x,pos.y),GetColorU32(Col_Text),suffix.c_str());x+=CalcTextSize(suffix.c_str()).x;}
    ItemSize(Vec2(x-pos.x,GOG->atlas.line_height));return true;
}
static std::string StripInlineMarkdown(const std::string& line) {
    std::string out;
    for (size_t i = 0; i < line.size(); ++i) {
        if (line[i] == '*' || line[i] == '`') continue;
        if (line[i] == '[') {
            size_t close = line.find(']', i + 1);
            if (close != std::string::npos && close + 1 < line.size() && line[close + 1] == '(') {
                out.append(line, i + 1, close - i - 1);
                size_t end = line.find(')', close + 2);
                if (end != std::string::npos) { i = end; continue; }
            }
        }
        out.push_back(line[i]);
    }
    return out;
}
static void MarkdownCodeLine(const std::string& line) {
    Window* w = GOG->cur_window;
    Style& s = GOG->style;
    float height = GOG->atlas.line_height + s.frame_padding.y * 2.0f;
    Vec2 pos = w->cursor;
    w->draw.AddRectFilledRounded(pos, Vec2(pos.x + w->content_w, pos.y + height), GetColorU32(Col_CodeBg), 4.0f);
    w->draw.AddText(Vec2(pos.x + s.frame_padding.x, pos.y + s.frame_padding.y), GetColorU32(Col_Link), line.c_str());
    ItemSize(Vec2(w->content_w, height));
}
static void MarkdownWrappedLine(const std::string& text, U32 color) {
    Window* w = GOG->cur_window;
    std::string line;
    size_t start = 0;
    while (start < text.size()) {
        while (start < text.size() && text[start] == ' ') ++start;
        size_t end = text.find(' ', start);
        if (end == std::string::npos) end = text.size();
        std::string word = text.substr(start, end - start);
        std::string candidate = line.empty() ? word : line + " " + word;
        if (!line.empty() && CalcTextSize(candidate.c_str()).x > w->content_w) {
            TextImpl(line.c_str(), 0, color);
            line = word;
        } else {
            line = candidate;
        }
        start = end + 1;
    }
    if (!line.empty()) TextImpl(line.c_str(), 0, color);
}
void Markdown(const char* markdown) {
    if (!markdown || !GOG || !GOG->cur_window) return;
    std::string source(markdown);
    bool code = false;
    size_t start = 0;
    while (start <= source.size()) {
        size_t end = source.find('\n', start);
        if (end == std::string::npos) end = source.size();
        std::string line = source.substr(start, end - start);
        if (!line.empty() && line[line.size() - 1] == '\r') line.erase(line.size() - 1);

        if (line.compare(0, 3, "```") == 0) {
            code = !code;
        } else if (code) {
            MarkdownCodeLine(line);
        } else if (line.empty()) {
            Spacing();
        } else if (line == "---" || line == "***") {
            Separator();
        } else if (line.compare(0, 2, "# ") == 0) {
            TextColored(Vec4(0.55f, 0.93f, 0.85f, 1.0f), "%s", StripInlineMarkdown(line.substr(2)).c_str());
            Separator();
        } else if (line.compare(0, 3, "## ") == 0) {
            TextColored(Vec4(0.63f, 0.56f, 1.0f, 1.0f), "%s", StripInlineMarkdown(line.substr(3)).c_str());
        } else if (line.compare(0, 2, "- ") == 0 || line.compare(0, 2, "* ") == 0) {
            BulletText("%s", StripInlineMarkdown(line.substr(2)).c_str());
        } else if (line.compare(0, 2, "> ") == 0) {
            Window* w = GOG->cur_window;
            Vec2 pos = w->cursor;
            w->draw.AddRectFilledRounded(pos, Vec2(pos.x + 3, pos.y + GOG->atlas.line_height), GetColorU32(Col_Link), 1.5f);
            w->cursor.x += 12.0f;
            TextDisabled("%s", StripInlineMarkdown(line.substr(2)).c_str());
        } else if (MarkdownInteractiveLine(line)) {
        } else {
            std::string clean = StripInlineMarkdown(line);
            MarkdownWrappedLine(clean, GetColorU32(Col_Text));
        }
        if (end == source.size()) break;
        start = end + 1;
    }
}

// =====================================================================
//  Node editor
// =====================================================================
struct NodeEditorState {
    bool active;
    bool node_active;
    Window* window;
    Vec4 rect;
    ID editor_id;
    ID node_id;
    Vec4 node_rect;
    float input_y;
    float output_y;
    NodeEditorState() : active(false), node_active(false), window(0), editor_id(0), node_id(0), input_y(0), output_y(0) {}
};
static NodeEditorState GNodeEditor;

bool BeginNodeEditor(const char* id, const Vec2& size_arg) {
    if (!GOG || !GOG->cur_window || GNodeEditor.active) return false;
    Window* w = GOG->cur_window;
    float width = size_arg.x > 0.0f ? size_arg.x : GetContentRegionAvail().x;
    float height = size_arg.y > 0.0f ? size_arg.y : 300.0f;
    Vec2 pos = w->cursor;
    ItemSize(Vec2(width, height));

    GNodeEditor.active = true;
    GNodeEditor.node_active = false;
    GNodeEditor.window = w;
    GNodeEditor.editor_id = GetID(id ? id : "node-editor");
    GNodeEditor.rect = Vec4(pos.x, pos.y, pos.x + width, pos.y + height);
    w->id_stack.push_back(GNodeEditor.editor_id);

    w->draw.AddRectFilledRounded(pos, Vec2(pos.x + width, pos.y + height), GetColorU32(Col_CodeBg), GOG->style.frame_rounding + 3.0f);
    w->draw.PushClipRect(GNodeEditor.rect);
    const float grid = 24.0f;
    for (float x = pos.x; x < pos.x + width; x += grid)
        w->draw.AddLine(Vec2(x, pos.y), Vec2(x, pos.y + height), GetColorU32(Col_NodeGrid), 1.0f);
    for (float y = pos.y; y < pos.y + height; y += grid)
        w->draw.AddLine(Vec2(pos.x, y), Vec2(pos.x + width, y), GetColorU32(Col_NodeGrid), 1.0f);
    return true;
}

bool BeginNode(int node_id, const char* title, Vec2* position, const Vec2& size_arg) {
    if (!GNodeEditor.active || GNodeEditor.node_active || !position) return false;
    Window* w = GNodeEditor.window;
    char id_buf[48];
    snprintf(id_buf, sizeof(id_buf), "node:%d", node_id);
    ID id = HashStr(id_buf, 0, GNodeEditor.editor_id);
    Vec2 pos(GNodeEditor.rect.x + position->x, GNodeEditor.rect.y + position->y);
    Vec2 size(Max(size_arg.x, 120.0f), Max(size_arg.y, 80.0f));
    Vec4 title_rect(pos.x, pos.y, pos.x + size.x, pos.y + 34.0f);

    bool hovered = false, held = false;
    ButtonBehavior(title_rect, id, &hovered, &held);
    if (held) {
        position->x += GOG->io.mouse_pos.x - GOG->mouse_pos_prev.x;
        position->y += GOG->io.mouse_pos.y - GOG->mouse_pos_prev.y;
        position->x = Clamp(position->x, 0.0f, Max(0.0f, (GNodeEditor.rect.z - GNodeEditor.rect.x) - size.x));
        position->y = Clamp(position->y, 0.0f, Max(0.0f, (GNodeEditor.rect.w - GNodeEditor.rect.y) - size.y));
        pos = Vec2(GNodeEditor.rect.x + position->x, GNodeEditor.rect.y + position->y);
        title_rect = Vec4(pos.x, pos.y, pos.x + size.x, pos.y + 34.0f);
    }

    Vec2 end(pos.x + size.x, pos.y + size.y);
    w->draw.AddShadowRect(pos, end, GetColorU32(Col_WindowShadow), 9.0f, held ? 12.0f : 8.0f);
    w->draw.AddBackdropBlur(pos, end, ColorWithAlpha(GetColorU32(Col_NodeBg), 220), 7.0f, 9.0f);
    U32 title_a = held ? GetColorU32(Col_ButtonHovered) : GetColorU32(Col_NodeTitle);
    w->draw.AddRectFilledRounded(pos, Vec2(end.x, pos.y + 38.0f), title_a, 9.0f);
    w->draw.AddRectFilled(Vec2(pos.x, pos.y + 29.0f), Vec2(end.x, pos.y + 38.0f), title_a);
    w->draw.AddText(Vec2(pos.x + 12.0f, pos.y + 9.0f), OG_COL32_WHITE, title ? title : "Node");
    w->draw.AddRect(pos, end, ColorWithAlpha(GetColorU32(Col_Border), hovered ? 230 : 150), 1.0f);

    GNodeEditor.node_active = true;
    GNodeEditor.node_id = id;
    GNodeEditor.node_rect = Vec4(pos.x, pos.y, end.x, end.y);
    GNodeEditor.input_y = pos.y + 55.0f;
    GNodeEditor.output_y = pos.y + 55.0f;
    return true;
}

NodePin NodeInput(const char* label) {
    if (!GNodeEditor.node_active) return NodePin();
    Window* w = GNodeEditor.window;
    Vec2 pin(GNodeEditor.node_rect.x, GNodeEditor.input_y);
    ID id = HashStr(label ? label : "input", 0, GNodeEditor.node_id ^ 0x1A2B3C4DULL);
    w->draw.AddCircleFilled(pin, 6.0f, GetColorU32(Col_Success), 16);
    w->draw.AddCircleFilled(pin, 2.5f, GetColorU32(Col_WindowBg), 12);
    w->draw.AddText(Vec2(pin.x + 12.0f, pin.y - GOG->atlas.line_height * 0.5f), GetColorU32(Col_Text), label ? label : "Input");
    GNodeEditor.input_y += 24.0f;
    return NodePin(id, pin);
}

NodePin NodeOutput(const char* label) {
    if (!GNodeEditor.node_active) return NodePin();
    Window* w = GNodeEditor.window;
    Vec2 pin(GNodeEditor.node_rect.z, GNodeEditor.output_y);
    ID id = HashStr(label ? label : "output", 0, GNodeEditor.node_id ^ 0x5E6F7788ULL);
    Vec2 ts = CalcTextSize(label ? label : "Output");
    w->draw.AddText(Vec2(pin.x - 12.0f - ts.x, pin.y - GOG->atlas.line_height * 0.5f), GetColorU32(Col_Text), label ? label : "Output");
    w->draw.AddCircleFilled(pin, 6.0f, GetColorU32(Col_NodeLink), 16);
    w->draw.AddCircleFilled(pin, 2.5f, GetColorU32(Col_WindowBg), 12);
    GNodeEditor.output_y += 24.0f;
    return NodePin(id, pin);
}

void EndNode() { GNodeEditor.node_active = false; }

void NodeLink(const NodePin& from, const NodePin& to, U32 color) {
    if (!GNodeEditor.active || from.id == 0 || to.id == 0) return;
    DrawList& draw = GNodeEditor.window->draw;
    if (!color) color = GetColorU32(Col_NodeLink);
    Vec2 p0 = from.position;
    Vec2 p3 = to.position;
    float bend = Max(55.0f, fabsf(p3.x - p0.x) * 0.45f);
    Vec2 p1(p0.x + bend, p0.y);
    Vec2 p2(p3.x - bend, p3.y);
    Vec2 previous = p0;
    for (int i = 1; i <= 24; ++i) {
        float t = (float)i / 24.0f;
        float u = 1.0f - t;
        Vec2 p(u*u*u*p0.x + 3*u*u*t*p1.x + 3*u*t*t*p2.x + t*t*t*p3.x,
               u*u*u*p0.y + 3*u*u*t*p1.y + 3*u*t*t*p2.y + t*t*t*p3.y);
        draw.AddLine(previous, p, color, 2.5f);
        previous = p;
    }
}

void EndNodeEditor() {
    if (!GNodeEditor.active) return;
    Window* w = GNodeEditor.window;
    if (GNodeEditor.node_active) EndNode();
    w->draw.PopClipRect();
    if (w->id_stack.size() > 1) w->id_stack.pop_back();
    GNodeEditor.active = false;
    GNodeEditor.window = 0;
}

static std::string JSONEscape(const std::string& value) {
    std::string out; for(size_t i=0;i<value.size();++i){char c=value[i];if(c=='\\'||c=='\"')out.push_back('\\');if(c!='\n'&&c!='\r')out.push_back(c);}return out;
}
static FILE* OpenFilePortable(const char* path, const char* mode) {
#ifdef _MSC_VER
    FILE* file = 0; return fopen_s(&file, path, mode) == 0 ? file : 0;
#else
    return fopen(path, mode);
#endif
}
bool SaveStateJSON(const char* path) {
    if(!GOG||!path||!*path)return false;FILE* f=OpenFilePortable(path,"wb");if(!f)return false;
    const Style& s=GOG->style;
    fprintf(f,"{\n  \"version\": 1,\n  \"uiScale\": %.4f,\n  \"theme\": {\n",GOG->ui_scale);
    fprintf(f,"    \"windowRounding\": %.4f, \"frameRounding\": %.4f, \"shadowSize\": %.4f, \"animationSpeed\": %.4f,\n",s.window_rounding,s.frame_rounding,s.shadow_size,s.animation_speed);
    fprintf(f,"    \"colors\": [");for(int i=0;i<Col_COUNT;++i)fprintf(f,"%s%u",i?", ":"",s.colors[i]);fprintf(f,"]\n  },\n  \"windows\": [\n");
    for(size_t i=0;i<GOG->windows.size();++i){Window* w=GOG->windows[i];std::string name=JSONEscape(w->name);fprintf(f,"    {\"name\": \"%s\", \"x\": %.3f, \"y\": %.3f, \"width\": %.3f, \"height\": %.3f, \"dock\": %d, \"collapsed\": %s}%s\n",name.c_str(),w->pos.x,w->pos.y,w->size_full.x,w->size_full.y,(int)w->dock_slot,w->collapsed?"true":"false",i+1<GOG->windows.size()?",":"");}
    fprintf(f,"  ]\n}\n");fclose(f);return true;
}
static bool JSONNumber(const std::string& source,const char* key,float& value,size_t start=0){std::string needle=std::string("\"")+key+"\"";size_t p=source.find(needle,start);if(p==std::string::npos)return false;p=source.find(':',p+needle.size());if(p==std::string::npos)return false;value=(float)strtod(source.c_str()+p+1,0);return true;}
bool LoadStateJSON(const char* path) {
    if(!GOG||!path)return false;FILE* f=OpenFilePortable(path,"rb");if(!f)return false;fseek(f,0,SEEK_END);long size=ftell(f);fseek(f,0,SEEK_SET);if(size<=0){fclose(f);return false;}std::string src((size_t)size,'\0');fread(&src[0],1,(size_t)size,f);fclose(f);
    float v=0;if(JSONNumber(src,"uiScale",v))SetUIScale(v);if(JSONNumber(src,"windowRounding",v))GOG->style.window_rounding=v;if(JSONNumber(src,"frameRounding",v))GOG->style.frame_rounding=v;if(JSONNumber(src,"shadowSize",v))GOG->style.shadow_size=v;if(JSONNumber(src,"animationSpeed",v))GOG->style.animation_speed=v;
    size_t colors=src.find("\"colors\"");if(colors!=std::string::npos){size_t p=src.find('[',colors);for(int i=0;i<Col_COUNT&&p!=std::string::npos;++i){char* end=0;unsigned long c=strtoul(src.c_str()+p+1,&end,10);if(end==src.c_str()+p+1)break;GOG->style.colors[i]=(U32)c;p=(size_t)(end-src.c_str());p=src.find_first_of(",]",p);}}
    size_t p=0;while((p=src.find("\"name\"",p))!=std::string::npos){size_t q1=src.find('"',src.find(':',p)+1),q2=q1==std::string::npos?q1:src.find('"',q1+1);if(q1==std::string::npos||q2==std::string::npos)break;std::string name=src.substr(q1+1,q2-q1-1);Window* w=FindWindow(name.c_str());if(!w)w=CreateWindowObj(name.c_str());float x=0,y=0,width=0,height=0,dock=0;if(JSONNumber(src,"x",x,q2))w->pos.x=x;if(JSONNumber(src,"y",y,q2))w->pos.y=y;if(JSONNumber(src,"width",width,q2))w->size_full.x=width;if(JSONNumber(src,"height",height,q2))w->size_full.y=height;if(JSONNumber(src,"dock",dock,q2))w->dock_slot=(DockSlot)(int)dock;size_t collapsed=src.find("\"collapsed\"",q2);if(collapsed!=std::string::npos)w->collapsed=src.compare(src.find(':',collapsed)+1,5," true")==0;p=q2+1;}
    GOG->theme_from=GOG->theme_target=GOG->style;return true;
}

} // namespace og
