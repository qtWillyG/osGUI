// osgui.cpp - core implementation (cf. imgui.cpp)
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

    Window() : id(0), collapsed(false), active_this_frame(false), focus_order(0),
               curr_line_height(0), prev_line_height(0), indent(0), content_w(0),
               scroll_y(0), scroll_max_y(0), scrollbar_active(false) {}
};

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
static ID HashStr(const char* s, const char* s_end, ID seed) {
    ID h = seed ? seed : 1469598103934665603ULL;
    if (!s_end) { while (*s) { h ^= (unsigned char)*s++; h *= 1099511628211ULL; } }
    else        { while (s < s_end) { h ^= (unsigned char)*s++; h *= 1099511628211ULL; } }
    return h;
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

// =====================================================================
//  DrawList
// =====================================================================
void DrawList::Clear() {
    vtx.clear(); idx.clear(); cmds.clear(); clip_stack.clear();
}
DrawCmd& DrawList::CurCmd() {
    if (cmds.empty()) {
        DrawCmd c;
        c.clip_rect = clip_stack.empty() ? Vec4(-8192, -8192, 8192, 8192) : clip_stack.back();
        c.tex_id = cur_tex; c.idx_offset = (unsigned)idx.size(); c.elem_count = 0;
        cmds.push_back(c);
    }
    return cmds.back();
}
static void NewCmd(DrawList* dl) {
    DrawCmd c;
    c.clip_rect = dl->clip_stack.empty() ? Vec4(-8192, -8192, 8192, 8192) : dl->clip_stack.back();
    c.tex_id = dl->cur_tex; c.idx_offset = (unsigned)dl->idx.size(); c.elem_count = 0;
    dl->cmds.push_back(c);
}
void DrawList::PushClipRect(const Vec4& r) { clip_stack.push_back(r); NewCmd(this); }
void DrawList::PopClipRect()               { if (!clip_stack.empty()) clip_stack.pop_back(); NewCmd(this); }
void DrawList::PrimReserve(int idx_count, int) { CurCmd().elem_count += idx_count; }

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
    for (const char* p = text; (text_end ? p < text_end : *p) && *p; ++p) {
        unsigned char c = (unsigned char)*p;
        if (c == '\n') { x = pos.x; y += a.line_height; continue; }
        if (c < 32 || c >= 128 || !a.glyph_valid[c]) c = '?';
        if (!a.glyph_valid[c]) c = ' ';
        Glyph& g = a.glyphs[c];
        if (c != ' ')
            AddGlyph(x + g.x0, y + g.y0, x + g.x1, y + g.y1, g.u0, g.v0, g.u1, g.v1, col);
        x += g.advance;
    }
}

// =====================================================================
//  Style
// =====================================================================
Style::Style() {
    window_padding   = Vec2(8, 8);
    frame_padding    = Vec2(4, 3);
    item_spacing     = Vec2(8, 4);
    item_inner_spacing = Vec2(4, 4);
    indent_spacing   = 18.0f;
    scrollbar_size   = 14.0f;
    grab_min_size    = 10.0f;
    window_title_height = 0.0f;
    colors[Col_Text]              = OG_COL32(255, 255, 255, 255);
    colors[Col_TextDisabled]      = OG_COL32(128, 128, 128, 255);
    colors[Col_WindowBg]          = OG_COL32(15, 15, 15, 248);
    colors[Col_TitleBg]           = OG_COL32(10, 10, 10, 255);
    colors[Col_TitleBgActive]     = OG_COL32(41, 74, 122, 255);
    colors[Col_MenuBarBg]         = OG_COL32(36, 36, 36, 255);
    colors[Col_Border]            = OG_COL32(110, 110, 128, 128);
    colors[Col_FrameBg]           = OG_COL32(41, 74, 122, 138);
    colors[Col_FrameBgHovered]    = OG_COL32(66, 150, 250, 102);
    colors[Col_FrameBgActive]     = OG_COL32(66, 150, 250, 171);
    colors[Col_Button]            = OG_COL32(66, 150, 250, 102);
    colors[Col_ButtonHovered]     = OG_COL32(66, 150, 250, 255);
    colors[Col_ButtonActive]      = OG_COL32(15, 135, 250, 255);
    colors[Col_Header]            = OG_COL32(66, 150, 250, 79);
    colors[Col_HeaderHovered]     = OG_COL32(66, 150, 250, 204);
    colors[Col_HeaderActive]      = OG_COL32(66, 150, 250, 255);
    colors[Col_CheckMark]         = OG_COL32(66, 150, 250, 255);
    colors[Col_SliderGrab]        = OG_COL32(61, 133, 224, 255);
    colors[Col_SliderGrabActive]  = OG_COL32(66, 150, 250, 255);
    colors[Col_Separator]         = OG_COL32(110, 110, 128, 128);
    colors[Col_ResizeGrip]        = OG_COL32(66, 150, 250, 51);
    colors[Col_ResizeGripHovered] = OG_COL32(66, 150, 250, 171);
    colors[Col_ResizeGripActive]  = OG_COL32(66, 150, 250, 242);
    colors[Col_ScrollbarBg]       = OG_COL32(5, 5, 5, 135);
    colors[Col_ScrollbarGrab]     = OG_COL32(79, 79, 79, 255);
    colors[Col_PlotLines]         = OG_COL32(156, 156, 156, 255);
    colors[Col_PlotHistogram]     = OG_COL32(230, 179, 0, 255);
}

Context::Context() {
    cur_window = hovered_window = moving_window = nav_window = 0;
    active_id = 0; active_id_window = 0; hovered_id = 0;
    frame_count = 0; focus_counter = 0; time = 0; framerate_acc = 60.0f;
    for (int i = 0; i < 3; i++) { mouse_down_prev[i] = mouse_clicked[i] = mouse_released[i] = false; }
    next_pos_set = next_size_set = false;
    memset(&io, 0, sizeof(io));
    io.framerate = 60.0f;
    atlas.pixels = 0; atlas.tex_id = 0;
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

    // store mouse for next-frame delta
    for (int i = 0; i < 3; i++) g.mouse_down_prev[i] = g.io.mouse_down[i];
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
    for (const char* p = text; (text_end ? p < text_end : *p) && *p; ++p) {
        unsigned char c = (unsigned char)*p;
        if (c == '\n') { if (line_w > max_w) max_w = line_w; line_w = 0; h += a.line_height; continue; }
        if (c < 32 || c >= 128) c = '?';
        line_w += a.glyphs[c].advance;
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
static bool ButtonBehavior(const Vec4& r, ID id, bool* out_hovered, bool* out_held) {
    Context& g = *GOG;
    bool hovered = ItemHoverable(r, id);
    bool pressed = false, held = false;
    if (hovered && g.mouse_clicked[0]) { g.active_id = id; g.active_id_window = g.cur_window; FocusWindow(g.cur_window); }
    if (g.active_id == id) {
        if (g.io.mouse_down[0]) held = true;
        else { if (hovered) pressed = true; g.active_id = 0; }
    }
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

bool Begin(const char* name, bool* p_open) {
    Context& g = *GOG;
    Style& s = g.style;
    Window* w = FindWindow(name);
    bool created = false;
    if (!w) { w = CreateWindowObj(name); created = true; }
    if (created && g.next_pos_set)  w->pos = g.next_pos;
    if (created && g.next_size_set) w->size_full = g.next_size;
    g.next_pos_set = g.next_size_set = false;
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

    dl->PushClipRect(Vec4(pos.x, pos.y, pos.x + size.x, pos.y + size.y));

    if (!w->collapsed)
        dl->AddRectFilled(Vec2(pos.x, pos.y + title_h), Vec2(pos.x + size.x, pos.y + size.y), GetColorU32(Col_WindowBg));
    dl->AddRectFilled(pos, Vec2(pos.x + size.x, pos.y + title_h), GetColorU32(focused ? Col_TitleBgActive : Col_TitleBg));

    // collapse arrow
    float asz = g.atlas.line_height * 0.7f;
    RenderArrow(dl, Vec2(pos.x + 6, pos.y + (title_h - asz) * 0.5f), asz, GetColorU32(Col_Text), w->collapsed ? 0 : 1);
    Vec4 arrow_rect(pos.x, pos.y, pos.x + title_h, pos.y + title_h);

    // title text
    const char* disp_end = FindDisplayEnd(name);
    dl->AddText(Vec2(pos.x + title_h, pos.y + s.frame_padding.y), GetColorU32(Col_Text), name, disp_end);

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
        g.moving_window = w; g.active_id = 0;
    }
    if (close_clicked && p_open) *p_open = false;

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
    U32 col = GetColorU32(held ? Col_ButtonActive : (hovered ? Col_ButtonHovered : Col_Button));
    w->draw.AddRectFilled(pos, Vec2(r.z, r.w), col);
    RenderTextClipped(r, label, end, true);
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
    if (pressed) *v = !*v;
    U32 bg = GetColorU32(held ? Col_FrameBgActive : (hovered ? Col_FrameBgHovered : Col_FrameBg));
    w->draw.AddRectFilled(Vec2(box.x, box.y), Vec2(box.z, box.w), bg);
    if (*v) {
        float pad = sq * 0.27f;
        float x0 = box.x + pad, x1 = box.z - pad, y0 = box.y + pad, y1 = box.w - pad;
        U32 cm = GetColorU32(Col_CheckMark);
        w->draw.AddLine(Vec2(x0, (y0 + y1) * 0.5f), Vec2(x0 + (x1 - x0) * 0.35f, y1), cm, 2.0f);
        w->draw.AddLine(Vec2(x0 + (x1 - x0) * 0.35f, y1), Vec2(x1, y0), cm, 2.0f);
    }
    w->draw.AddText(Vec2(box.z + s.item_inner_spacing.x, pos.y + s.frame_padding.y), GetColorU32(Col_Text), label, end);
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
    if (pressed) *v = v_button;
    Vec2 c(pos.x + sq * 0.5f, pos.y + sq * 0.5f);
    U32 bg = GetColorU32(held ? Col_FrameBgActive : (hovered ? Col_FrameBgHovered : Col_FrameBg));
    w->draw.AddCircleFilled(c, sq * 0.5f, bg, 16);
    if (*v == v_button) w->draw.AddCircleFilled(c, sq * 0.25f, GetColorU32(Col_CheckMark), 16);
    w->draw.AddText(Vec2(pos.x + sq + s.item_inner_spacing.x, pos.y + s.frame_padding.y), GetColorU32(Col_Text), label, end);
    return pressed;
}

static bool SliderBehavior(const Vec4& r, ID id, float* v, float v_min, float v_max, bool* out_held) {
    Context& g = *GOG;
    bool hovered = ItemHoverable(r, id);
    bool changed = false, held = false;
    if (hovered && g.mouse_clicked[0]) { g.active_id = id; g.active_id_window = g.cur_window; FocusWindow(g.cur_window); }
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
    w->draw.AddRectFilled(Vec2(frame.x, frame.y), Vec2(frame.z, frame.w),
                          GetColorU32(held ? Col_FrameBgActive : Col_FrameBg));
    // grab
    float grab = s.grab_min_size;
    float t = (v_max > v_min) ? (*v - v_min) / (v_max - v_min) : 0.0f;
    float gx = frame.x + t * ((frame.z - frame.x) - grab);
    w->draw.AddRectFilled(Vec2(gx, frame.y + 2), Vec2(gx + grab, frame.w - 2),
                          GetColorU32(held ? Col_SliderGrabActive : Col_SliderGrab));
    // value text
    char buf[64];
    if (is_int) snprintf(buf, sizeof(buf), "%d", (int)(*v + (*v >= 0 ? 0.5f : -0.5f)));
    else        snprintf(buf, sizeof(buf), fmt, *v);
    RenderTextClipped(frame, buf, 0, true);
    // label
    w->draw.AddText(Vec2(frame.z + s.item_inner_spacing.x, pos.y + s.frame_padding.y), GetColorU32(Col_Text), label, end);
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
    Style& s = GOG->style;
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
    w->draw.AddRectFilled(pos, Vec2(r.z, r.w), GetColorU32(Col_FrameBg));
    w->draw.AddRectFilled(pos, Vec2(pos.x + fw * fraction, r.w), GetColorU32(Col_Button));
    char buf[32];
    if (!overlay) { snprintf(buf, sizeof(buf), "%.0f%%", fraction * 100.0f); overlay = buf; }
    RenderTextClipped(r, overlay, 0, true);
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

} // namespace og
