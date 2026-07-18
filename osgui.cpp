// OSGui core implementation: layout, interaction, widgets, and draw data.
#include "osgui.h"
#include <math.h>
#include <string.h>
#include <stdio.h>
#include <stdarg.h>
#include <stdlib.h>
#include <float.h>
#include <algorithm>
#include <limits>
#include <sstream>
#include <iomanip>
#include <locale>

namespace og {

// =====================================================================
//  Globals
// =====================================================================
static thread_local Context* GOG = 0;
static void CleanupContextState(Context* context);
const char* GetVersion() { return OSGUI_VERSION; }
Context* GetCurrentContext() { return GOG; }
void SetCurrentContext(Context* context) { GOG = context; }
Context& GetContext() { return *GOG; }
IO&        GetIO()        { return GOG->io; }
Style&     GetStyle()     { return GOG->style; }
FontAtlas& GetFontAtlas() { return GOG->atlas; }
DrawData*  GetDrawData()  { return &GOG->draw_data; }
const std::vector<Event>& GetEvents() { return GOG->events; }
const FrameMetrics& GetFrameMetrics() { return GOG->metrics; }
U32 GetColorU32(int idx) {
    if (!GOG || idx < 0 || idx >= Col_COUNT) return 0;
    U32 color = GOG->style.colors[idx];
    if (GOG->disabled_depth > 0) {
        int alpha = (int)(((color >> 24) & 255) * GOG->style.disabled_alpha);
        color = (color & 0x00FFFFFFu) | ((U32)alpha << 24);
    }
    return color;
}

bool DebugCheckVersionAndDataLayout(const char* version, size_t io_size, size_t style_size,
                                    size_t draw_vert_size, size_t draw_idx_size) {
    bool ok = version && strcmp(version, OSGUI_VERSION) == 0 && io_size == sizeof(IO) &&
              style_size == sizeof(Style) && draw_vert_size == sizeof(DrawVert) &&
              draw_idx_size == sizeof(DrawIdx);
    if (!ok && GOG) {
        GOG->last_error = "OSGui version or data-layout mismatch";
        if (GOG->debug_log_callback)
            GOG->debug_log_callback(GOG->last_error.c_str(), GOG->debug_log_user_data);
    }
    return ok;
}

// =====================================================================
//  Window (persistent per-name state)
// =====================================================================
struct ChildScrollRegion {
    Vec4 rect;
    ID id;
    int depth;
    ChildScrollRegion(const Vec4& value_rect, ID value_id, int value_depth)
        : rect(value_rect), id(value_id), depth(value_depth) {}
};
struct Window {
    ID          id;
    std::string name;
    Vec2        pos;
    Vec2        size;        // size used this frame
    Vec2        size_full;   // size when expanded
    bool        collapsed;
    bool        active_this_frame;
    bool        active_last_frame;
    int         focus_order;
    DockSlot    dock_slot;
    int         flags;
    bool        appeared_this_frame;

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
    std::vector<ChildScrollRegion> child_scroll_regions;
    ID          wheel_child_id;
    bool        wheel_child_consumed;
    bool        skip_items;
    Window*     popup_parent;

    DrawList    draw;
    std::vector<ID> id_stack;

    Window() : id(0), collapsed(false), active_this_frame(false), active_last_frame(false), focus_order(0), dock_slot(Dock_None),
               flags(WindowFlags_None), appeared_this_frame(false),
               curr_line_height(0), prev_line_height(0), indent(0), content_w(0),
               scroll_y(0), scroll_max_y(0), scrollbar_active(false), wheel_child_id(0),
               wheel_child_consumed(false), skip_items(false), popup_parent(0) {}
};

static bool WindowDescendsFrom(Window* window, Window* ancestor) {
    if (!ancestor) return false;
    for (Window* current = window; current; current = current->popup_parent)
        if (current == ancestor) return true;
    return false;
}
static bool WindowInModalTree(const Context& context, Window* window) {
    return !context.modal_window || WindowDescendsFrom(window, context.modal_window);
}
static Window* WindowAtPoint(const Vec2& point);
static bool ItemContainsPoint(const Vec4& rect, const Vec2& point, Window* window);

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
static thread_local std::map<Context*, std::vector<GridState> > GGridStacks;
static thread_local std::map<Context*, DockSlot> GNextDockSlots;
static thread_local std::map<Context*, bool> GNextDockSets;
static std::vector<GridState>& GridStack() { return GGridStacks[GOG]; }
static DockSlot& NextDockSlot() { return GNextDockSlots[GOG]; }
static bool& NextDockSet() { return GNextDockSets[GOG]; }

// =====================================================================
//  Small helpers
// =====================================================================
static inline float Min(float a, float b) { return a < b ? a : b; }
static inline float Max(float a, float b) { return a > b ? a : b; }
static inline float Clamp(float v, float lo, float hi) { return v < lo ? lo : (v > hi ? hi : v); }
static inline bool FiniteFloat(float v) { return v == v && v <= FLT_MAX && v >= -FLT_MAX; }
static const int WindowFlags_InternalModal = 1 << 29;
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
static ID HashLabelID(const char* label, ID seed) {
    if (!label) return 0;
    const char* stable = strstr(label, "###");
    return HashStr(stable ? stable + 3 : label, 0, seed);
}
static unsigned int DecodeUTF8(const char*& p, const char* end) {
    if (!p || (end && p >= end) || !*p) return 0;
    const char* start=p;const unsigned char* s=(const unsigned char*)start;unsigned int cp=0,min_value=0;int bytes=1;
    if(s[0]<0x80){cp=s[0];min_value=0;}
    else if((s[0]&0xE0)==0xC0){cp=s[0]&0x1F;bytes=2;min_value=0x80;}
    else if((s[0]&0xF0)==0xE0){cp=s[0]&0x0F;bytes=3;min_value=0x800;}
    else if((s[0]&0xF8)==0xF0){cp=s[0]&0x07;bytes=4;min_value=0x10000;}
    else{++p;return 0xFFFD;}
    for (int i = 1; i < bytes; ++i) {
        if ((end && start + i >= end) || !s[i] || (s[i] & 0xC0) != 0x80) { p=start+1; return 0xFFFD; }
        cp = (cp << 6) | (s[i] & 0x3F);
    }
    if(cp<min_value||cp>0x10FFFF||(cp>=0xD800&&cp<=0xDFFF)){p=start+1;return 0xFFFD;}
    p = start + bytes;
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
// "text##suffix" hides the suffix but hashes the full label; "text###stable"
// also keeps identity stable when the visible prefix changes.
static const char* FindDisplayEnd(const char* label) {
    const char* p = label;
    while (*p) { if (p[0] == '#' && p[1] == '#') return p; p++; }
    return p;
}
ID GetID(const char* label) {
    if (!GOG || !label) return 0;
    ID seed = 0;
    if (GOG->cur_window && !GOG->cur_window->id_stack.empty())
        seed = GOG->cur_window->id_stack.back();
    return HashLabelID(label, seed);
}
void PushID(const char* value) {
    if (!GOG || !GOG->cur_window) return;
    Window* w = GOG->cur_window;
    w->id_stack.push_back(HashStr(value ? value : "", 0, w->id_stack.back()));
}
void PushID(int value) {
    char buffer[48];
    snprintf(buffer, sizeof(buffer), "#%d", value);
    PushID(buffer);
}
void PushID(const void* value) {
    char buffer[64];
    snprintf(buffer, sizeof(buffer), "#%p", value);
    PushID(buffer);
}
void PopID() {
    if (!GOG || !GOG->cur_window) return;
    std::vector<ID>& stack = GOG->cur_window->id_stack;
    if (stack.size() > 1) stack.pop_back();
    else {
        GOG->last_error = "PopID() called with an empty user ID stack";
        if (GOG->debug_log_callback) GOG->debug_log_callback(GOG->last_error.c_str(), GOG->debug_log_user_data);
    }
}
static float AnimateID(ID id, float target, float speed) {
    Context& g = *GOG;
    AnimationState& state = g.animations[id];
    if (state.last_frame == 0) state.value = target;
    state.target = target;
    state.last_frame = g.frame_count;
    if (g.io.config_reduced_motion || g.style.motion_scale <= 0.0f) {
        state.value = state.target;
        state.velocity = 0.0f;
        return state.value;
    }
    if (speed <= 0.0f) speed = g.style.animation_speed;
    speed /= Max(g.style.motion_scale, 0.01f);
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
//  Input queue and diagnostics
// =====================================================================
IO::IO()
    : display_size(), framebuffer_scale(1.0f, 1.0f), dpi_scale(1.0f), delta_time(1.0f / 60.0f),
      mouse_pos(-1.0f, -1.0f), mouse_wheel(0.0f), mouse_wheel_h(0.0f),
      get_clipboard_text(0), set_clipboard_text(0), clipboard_user_data(0),
      backend_platform_name(0), backend_renderer_name(0), backend_flags(BackendFlags_None),
      app_focused(true), config_reduced_motion(false), framerate(60.0f),
      want_capture_mouse(false), want_capture_keyboard(false), want_text_input(false) {
    for (int i = 0; i < 5; ++i) mouse_down[i] = false;
    for (int i = 0; i < Key_COUNT; ++i) key_down[i] = false;
}
void IO::AddMousePosEvent(float x, float y) {
    mouse_pos = Vec2(x, y); InputEvent e; e.type = InputEvent_MousePos; e.x = x; e.y = y; input_events.push_back(e);
}
void IO::AddMouseButtonEvent(int button, bool value) {
    if (button < 0 || button >= 5) return;
    mouse_down[button] = value; InputEvent e; e.type = InputEvent_MouseButton; e.key_or_button = button; e.down = value;
    e.x = mouse_pos.x; e.y = mouse_pos.y; input_events.push_back(e);
}
void IO::AddMouseWheelEvent(float horizontal, float vertical) {
    mouse_wheel_h += horizontal; mouse_wheel += vertical; InputEvent e; e.type = InputEvent_MouseWheel; e.x = horizontal; e.y = vertical; input_events.push_back(e);
}
void IO::AddKeyEvent(Key key, bool value) {
    int index = (int)key; if (index <= 0 || index >= Key_COUNT) return;
    key_down[index] = value; InputEvent e; e.type = InputEvent_Key; e.key_or_button = index; e.down = value; input_events.push_back(e);
}
void IO::AddInputCharacter(unsigned int codepoint) {
    if (codepoint == 0 || codepoint > 0x10FFFF || (codepoint >= 0xD800 && codepoint <= 0xDFFF)) return;
    input_chars.push_back(codepoint); InputEvent e; e.type = InputEvent_Text; e.codepoint = codepoint; input_events.push_back(e);
}
void IO::AddFocusEvent(bool focused) {
    app_focused = focused; InputEvent e; e.type = InputEvent_Focus; e.down = focused; input_events.push_back(e);
    if (!focused) {
        for (int i = 0; i < 5; ++i) mouse_down[i] = false;
        for (int i = 0; i < Key_COUNT; ++i) key_down[i] = false;
    }
}
void IO::ClearInputEvents() { input_events.clear(); }

FrameMetrics::FrameMetrics()
    : frame_number(0), active_windows(0), items_submitted(0), clipped_items(0), id_conflicts(0),
      draw_lists(0), draw_commands(0), vertices(0), indices(0), input_events(0), ui_events(0),
      animation_states(0) {}

// =====================================================================
//  DrawList
// =====================================================================
DrawCmd::DrawCmd()
    : clip_rect(-8192, -8192, 8192, 8192), tex_id(0), idx_offset(0), vtx_offset(0),
      elem_count(0), effect(DrawEffect_None), effect_amount(0.0f), callback(0), callback_data(0) {}
DrawList::DrawList():cur_tex(0),cur_effect(DrawEffect_None),cur_effect_amount(0.0f),white_uv(){}

void DrawList::Clear() {
    vtx.clear(); idx.clear(); cmds.clear(); clip_stack.clear(); texture_stack.clear(); effect_stack.clear(); effect_amount_stack.clear();
    cur_tex = 0;
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
void DrawList::PushClipRect(const Vec4& r, bool intersect_with_current) {
    Vec4 clipped = r;
    if (intersect_with_current && !clip_stack.empty()) {
        const Vec4& parent = clip_stack.back();
        clipped.x = Max(clipped.x, parent.x); clipped.y = Max(clipped.y, parent.y);
        clipped.z = Min(clipped.z, parent.z); clipped.w = Min(clipped.w, parent.w);
        if (clipped.z < clipped.x) clipped.z = clipped.x;
        if (clipped.w < clipped.y) clipped.w = clipped.y;
    }
    clip_stack.push_back(clipped); NewCmd(this);
}
void DrawList::PopClipRect()               { if (!clip_stack.empty()) clip_stack.pop_back(); NewCmd(this); }
void DrawList::PrimReserve(int idx_count, int vtx_count) {
    if (idx_count <= 0 || vtx_count < 0) return;
    size_t required_indices = idx.size() + (size_t)idx_count;
    size_t required_vertices = vtx.size() + (size_t)vtx_count;
    if (required_indices > idx.capacity())
        idx.reserve(std::max(required_indices, idx.capacity() + idx.capacity() / 2 + 256));
    if (required_vertices > vtx.capacity())
        vtx.reserve(std::max(required_vertices, vtx.capacity() + vtx.capacity() / 2 + 128));
    CurCmd().elem_count += (unsigned int)idx_count;
}
void DrawList::PushEffect(int effect, float amount) {
    effect_stack.push_back(cur_effect); effect_amount_stack.push_back(cur_effect_amount);
    cur_effect = effect;
    cur_effect_amount = amount;
    NewCmd(this);
}
void DrawList::PopEffect() {
    if (effect_stack.empty() || effect_amount_stack.empty()) return;
    cur_effect = effect_stack.back(); effect_stack.pop_back();
    cur_effect_amount = effect_amount_stack.back(); effect_amount_stack.pop_back();
    NewCmd(this);
}
void DrawList::PushTexture(TextureID texture_id) {
    texture_stack.push_back(cur_tex); cur_tex = texture_id; NewCmd(this);
}
void DrawList::PopTexture() {
    if (texture_stack.empty()) return;
    cur_tex = texture_stack.back(); texture_stack.pop_back(); NewCmd(this);
}
void DrawList::AddCallback(DrawCallback callback_value, void* data) {
    if (!callback_value) return;
    DrawCmd command;
    command.clip_rect = clip_stack.empty() ? Vec4(-8192, -8192, 8192, 8192) : clip_stack.back();
    command.tex_id = cur_tex; command.idx_offset = (unsigned)idx.size();
    command.callback = callback_value; command.callback_data = data;
    cmds.push_back(command); NewCmd(this);
}
static bool SameRect(const Vec4& a, const Vec4& b) {
    return a.x == b.x && a.y == b.y && a.z == b.z && a.w == b.w;
}
void DrawList::CompactCommands() {
    std::vector<DrawCmd> compact;
    compact.reserve(cmds.size());
    for (size_t i = 0; i < cmds.size(); ++i) {
        const DrawCmd& command = cmds[i];
        if (!command.callback && command.elem_count == 0) continue;
        if (!compact.empty() && !command.callback && !compact.back().callback &&
            compact.back().tex_id == command.tex_id && compact.back().effect == command.effect &&
            compact.back().vtx_offset == command.vtx_offset &&
            compact.back().effect_amount == command.effect_amount && SameRect(compact.back().clip_rect, command.clip_rect) &&
            compact.back().idx_offset + compact.back().elem_count == command.idx_offset) {
            compact.back().elem_count += command.elem_count;
        } else compact.push_back(command);
    }
    cmds.swap(compact);
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
    if (!FiniteFloat(w) || !FiniteFloat(h) || w <= 0.0f || h <= 0.0f) return;
    float r = Min(radius, Min(w, h) * 0.5f);
    if (r <= 1.0f) { AddRectFilled(a, b, col); return; }

    // A single convex fan keeps translucent fills uniform. Composing strips and
    // circles would blend overlapping pixels more than once at every corner.
    int corner_segments = (int)(r * 0.35f) + 3;
    if (corner_segments < 3) corner_segments = 3;
    if (corner_segments > 12) corner_segments = 12;
    const int perimeter_count = 4 * (corner_segments + 1);
    DrawIdx base = (DrawIdx)vtx.size();
    PrimReserve(perimeter_count * 3, perimeter_count + 1);
    DrawVert vertex; vertex.col = col; vertex.uv = white_uv;
    vertex.pos = Vec2((a.x + b.x) * 0.5f, (a.y + b.y) * 0.5f);
    vtx.push_back(vertex);
    const Vec2 centers[4] = {
        Vec2(b.x - r, a.y + r), Vec2(b.x - r, b.y - r),
        Vec2(a.x + r, b.y - r), Vec2(a.x + r, a.y + r)
    };
    const float half_pi = 1.57079632679f;
    for (int corner = 0; corner < 4; ++corner) {
        float start = -half_pi + corner * half_pi;
        for (int segment = 0; segment <= corner_segments; ++segment) {
            float angle = start + half_pi * (float)segment / (float)corner_segments;
            vertex.pos = Vec2(centers[corner].x + cosf(angle) * r,
                              centers[corner].y + sinf(angle) * r);
            vtx.push_back(vertex);
        }
    }
    for (int point = 0; point < perimeter_count; ++point) {
        idx.push_back(base);
        idx.push_back((DrawIdx)(base + 1 + point));
        idx.push_back((DrawIdx)(base + 1 + ((point + 1) % perimeter_count)));
    }
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
    if (r <= 0.0f || segs < 3) return;
    for (int i = 0; i < segs; i++) {
        float a0 = (float)i / segs * 6.2831853f;
        float a1 = (float)(i + 1) / segs * 6.2831853f;
        AddTriangleFilled(c, Vec2(c.x + cosf(a0) * r, c.y + sinf(a0) * r),
                             Vec2(c.x + cosf(a1) * r, c.y + sinf(a1) * r), col);
    }
}
void DrawList::AddCircle(const Vec2& c, float r, U32 col, int segs, float thickness) {
    if (r <= 0.0f || thickness <= 0.0f || segs < 3) return;
    Vec2 previous(c.x + r, c.y);
    for (int i = 1; i <= segs; ++i) {
        float angle = (float)i / (float)segs * 6.2831853f;
        Vec2 point(c.x + cosf(angle) * r, c.y + sinf(angle) * r);
        AddLine(previous, point, col, thickness); previous = point;
    }
}
void DrawList::AddBezierCubic(const Vec2& p1, const Vec2& p2, const Vec2& p3, const Vec2& p4,
                              U32 col, float thickness, int segments) {
    if (thickness <= 0.0f) return;
    if (segments <= 0) {
        float length_hint = fabsf(p4.x - p1.x) + fabsf(p4.y - p1.y) +
                            fabsf(p2.x - p1.x) + fabsf(p3.x - p4.x);
        segments = (int)Clamp(length_hint * 0.08f, 8.0f, 48.0f);
    }
    Vec2 previous = p1;
    for (int i = 1; i <= segments; ++i) {
        float t = (float)i / (float)segments, u = 1.0f - t;
        Vec2 point(u*u*u*p1.x + 3*u*u*t*p2.x + 3*u*t*t*p3.x + t*t*t*p4.x,
                   u*u*u*p1.y + 3*u*u*t*p2.y + 3*u*t*t*p3.y + t*t*t*p4.y);
        AddLine(previous, point, col, thickness); previous = point;
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

DrawDataSnapshot::DrawDataSnapshot(){data.display_pos=Vec2();data.display_size=Vec2();data.total_vtx=data.total_idx=0;}
DrawDataSnapshot::DrawDataSnapshot(const DrawDataSnapshot& other):owned_lists(other.owned_lists),data(other.data){data.lists.clear();for(size_t i=0;i<owned_lists.size();++i)data.lists.push_back(&owned_lists[i]);}
DrawDataSnapshot& DrawDataSnapshot::operator=(const DrawDataSnapshot& other){if(this!=&other){owned_lists=other.owned_lists;data=other.data;data.lists.clear();for(size_t i=0;i<owned_lists.size();++i)data.lists.push_back(&owned_lists[i]);}return *this;}
void DrawDataSnapshot::Clear() { owned_lists.clear(); data.lists.clear(); data.display_pos=Vec2();data.display_size=Vec2();data.total_vtx = data.total_idx = 0; }
void DrawDataSnapshot::Capture(const DrawData* source) {
    Clear(); if (!source) return;
    data.display_pos = source->display_pos; data.display_size = source->display_size;
    data.total_vtx = source->total_vtx; data.total_idx = source->total_idx;
    owned_lists.reserve(source->lists.size());
    for (size_t i = 0; i < source->lists.size(); ++i) owned_lists.push_back(*source->lists[i]);
    data.lists.reserve(owned_lists.size());
    for (size_t i = 0; i < owned_lists.size(); ++i) data.lists.push_back(&owned_lists[i]);
}

FontAtlas::FontAtlas():pixels(0),width(0),height(0),line_height(16.0f),ascent(12.0f),tex_id(0),white_uv(){memset(glyphs,0,sizeof(glyphs));memset(glyph_valid,0,sizeof(glyph_valid));}

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
    disabled_alpha = 0.46f;
    motion_scale = 1.0f;
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
    colors[Col_Info]              = OG_COL32(91, 188, 255, 255);
    colors[Col_Error]             = OG_COL32(238, 82, 106, 255);
    colors[Col_FocusRing]         = OG_COL32(153, 137, 255, 255);
    colors[Col_Selection]         = OG_COL32(116, 92, 255, 105);
    colors[Col_ChildBg]           = OG_COL32(15, 17, 27, 190);
    colors[Col_DragDropTarget]    = OG_COL32(247, 193, 96, 255);
    colors[Col_ModalDim]          = OG_COL32(3, 4, 9, 150);
}

Style GetBuiltinTheme(ThemePreset preset) {
    Style s;
    if (preset == Theme_Dark) return s;

    if (preset == Theme_HighContrast) {
        s.window_rounding = 3.0f; s.frame_rounding = 2.0f; s.shadow_size = 5.0f;
        s.colors[Col_Text] = OG_COL32(255,255,255,255);
        s.colors[Col_TextDisabled] = OG_COL32(198,198,210,255);
        s.colors[Col_WindowBg] = OG_COL32(7,8,12,255);
        s.colors[Col_TitleBg] = s.colors[Col_TitleBgActive] = OG_COL32(15,16,23,255);
        s.colors[Col_FrameBg] = OG_COL32(28,30,38,255);
        s.colors[Col_FrameBgHovered] = OG_COL32(48,51,63,255);
        s.colors[Col_FrameBgActive] = OG_COL32(64,68,82,255);
        s.colors[Col_Button] = OG_COL32(0,112,255,255);
        s.colors[Col_ButtonHovered] = OG_COL32(42,142,255,255);
        s.colors[Col_ButtonActive] = OG_COL32(0,83,210,255);
        s.colors[Col_Border] = OG_COL32(235,237,245,245);
        s.colors[Col_CheckMark] = OG_COL32(75,255,184,255);
        s.colors[Col_FocusRing] = OG_COL32(255,225,64,255);
        s.colors[Col_Selection] = OG_COL32(0,112,255,150);
        return s;
    }

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
    s.colors[Col_ChildBg]           = OG_COL32(235, 238, 247, 210);
    s.colors[Col_ModalDim]          = OG_COL32(34, 38, 55, 92);
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
    g.theme_duration = (g.io.config_reduced_motion || g.style.motion_scale <= 0.0f)
        ? 0.0f : Max(transition_seconds * g.style.motion_scale, 0.0f);
    g.theme_transitioning = g.theme_duration > 0.0f;
    g.theme_preset = preset;
    if (!g.theme_transitioning) g.style = g.theme_target;
}
bool IsThemeTransitioning() { return GOG && GOG->theme_transitioning; }

void SetUIScale(float scale) {
    if (!GOG) return;
    scale = Clamp(scale, 0.50f, 4.0f);
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
void SetReducedMotion(bool reduced) { if (GOG) GOG->io.config_reduced_motion = reduced; }
bool IsReducedMotion() { return GOG && GOG->io.config_reduced_motion; }
bool IsKeyDown(Key key) { return GOG && (int)key > 0 && (int)key < Key_COUNT && GOG->io.key_down[(int)key]; }
bool IsKeyPressed(Key key, bool repeat) {
    if (!GOG || (int)key <= 0 || (int)key >= Key_COUNT) return false;
    if (GOG->key_consumed[(int)key]) return false;
    if (GOG->key_pressed[(int)key]) return true;
    if (!repeat || !GOG->io.key_down[(int)key]) return false;
    float held=GOG->key_down_duration[(int)key],previous=held-GOG->io.delta_time;
    if(held<0.35f)return false;int current_tick=(int)((held-0.35f)/0.06f),previous_tick=previous<0.35f?-1:(int)((previous-0.35f)/0.06f);return current_tick!=previous_tick;
}
bool IsKeyPressed(int key) { return IsKeyPressed((Key)key, false); }
bool IsMouseDown(int button) { return GOG && button >= 0 && button < 5 && GOG->io.mouse_down[button]; }
bool IsMouseClicked(int button) { return GOG && button >= 0 && button < 5 && GOG->mouse_clicked[button]; }
bool IsMouseReleased(int button) { return GOG && button >= 0 && button < 5 && GOG->mouse_released[button]; }
Vec2 GetMousePos() { return GOG ? GOG->io.mouse_pos : Vec2(); }
Vec2 GetMouseDragDelta(int button, float lock_threshold) {
    if (!GOG || button < 0 || button >= 5 || !GOG->io.mouse_down[button]) return Vec2();
    Vec2 delta(GOG->io.mouse_pos.x - GOG->mouse_clicked_pos[button].x, GOG->io.mouse_pos.y - GOG->mouse_clicked_pos[button].y);
    float threshold = lock_threshold < 0.0f ? 0.0f : lock_threshold;
    return delta.x * delta.x + delta.y * delta.y >= threshold * threshold ? delta : Vec2();
}

void PushStyleColor(int color_index, U32 color) {
    if (!GOG || color_index < 0 || color_index >= Col_COUNT) return;
    GOG->style_color_stack.push_back(StyleColorBackup(color_index, GOG->style.colors[color_index]));
    GOG->style.colors[color_index] = color;
}
void PopStyleColor(int count) {
    if (!GOG) return;
    while (count-- > 0 && !GOG->style_color_stack.empty()) {
        StyleColorBackup backup = GOG->style_color_stack.back(); GOG->style_color_stack.pop_back();
        GOG->style.colors[backup.index] = backup.value;
    }
}
static float* StyleVarFloat(Style& style, StyleVar var) {
    switch (var) {
    case StyleVar_IndentSpacing: return &style.indent_spacing;
    case StyleVar_ScrollbarSize: return &style.scrollbar_size;
    case StyleVar_GrabMinSize: return &style.grab_min_size;
    case StyleVar_WindowRounding: return &style.window_rounding;
    case StyleVar_FrameRounding: return &style.frame_rounding;
    case StyleVar_ShadowSize: return &style.shadow_size;
    case StyleVar_AnimationSpeed: return &style.animation_speed;
    case StyleVar_DisabledAlpha: return &style.disabled_alpha;
    case StyleVar_MotionScale: return &style.motion_scale;
    default: return 0;
    }
}
static Vec2* StyleVarVec2(Style& style, StyleVar var) {
    switch (var) {
    case StyleVar_WindowPadding: return &style.window_padding;
    case StyleVar_FramePadding: return &style.frame_padding;
    case StyleVar_ItemSpacing: return &style.item_spacing;
    case StyleVar_ItemInnerSpacing: return &style.item_inner_spacing;
    default: return 0;
    }
}
void PushStyleVar(StyleVar var, float value) {
    if (!GOG) return; float* target = StyleVarFloat(GOG->style, var); if (!target) return;
    StyleVarBackup backup; backup.index = var; backup.float_value = *target; backup.is_vec2 = false;
    GOG->style_var_stack.push_back(backup); *target = value;
}
void PushStyleVar(StyleVar var, const Vec2& value) {
    if (!GOG) return; Vec2* target = StyleVarVec2(GOG->style, var); if (!target) return;
    StyleVarBackup backup; backup.index = var; backup.vec_value = *target; backup.is_vec2 = true;
    GOG->style_var_stack.push_back(backup); *target = value;
}
void PopStyleVar(int count) {
    if (!GOG) return;
    while (count-- > 0 && !GOG->style_var_stack.empty()) {
        StyleVarBackup backup = GOG->style_var_stack.back(); GOG->style_var_stack.pop_back();
        if (backup.is_vec2) { Vec2* target = StyleVarVec2(GOG->style, backup.index); if (target) *target = backup.vec_value; }
        else { float* target = StyleVarFloat(GOG->style, backup.index); if (target) *target = backup.float_value; }
    }
}
void BeginDisabled(bool disabled) { if (GOG) { GOG->disabled_stack.push_back(disabled); if (disabled) ++GOG->disabled_depth; } }
void EndDisabled() {
    if (!GOG || GOG->disabled_stack.empty()) return;
    bool disabled = GOG->disabled_stack.back(); GOG->disabled_stack.pop_back();
    if (disabled && GOG->disabled_depth > 0) --GOG->disabled_depth;
}
void PushItemWidth(float width) { if (GOG) GOG->item_width_stack.push_back(width); }
void PopItemWidth() { if (GOG && !GOG->item_width_stack.empty()) GOG->item_width_stack.pop_back(); }
void SetNextItemWidth(float width) { if (GOG) { GOG->next_item_width = width; GOG->next_item_width_set = true; } }

Context::Context() {
    cur_window = hovered_window = moving_window = nav_window = modal_window = 0;
    active_id = 0; text_active_id = 0; active_id_window = text_active_id_window = 0; hovered_id = 0;
    frame_count = 0; focus_counter = 0; time = 0; framerate_acc = 60.0f;
    focus_lost_this_frame = false;
    for (int i = 0; i < 5; i++) {
        mouse_down_prev[i] = mouse_clicked[i] = mouse_released[i] = false;
        mouse_clicked_pos[i] = mouse_released_pos[i] = Vec2();
        mouse_clicked_mods[i] = mouse_released_mods[i] = 0;
    }
    for (int i = 0; i < Key_COUNT; ++i) {
        key_down_prev[i] = key_pressed[i] = key_consumed[i] = false;
        key_pressed_mods[i] = 0;
        key_down_duration[i] = -1.0f;
    }
    next_pos_set = next_size_set = next_constraints_set = false;
    next_pos_cond = next_size_cond = Cond_Once;
    next_size_min = Vec2(0, 0); next_size_max = Vec2(FLT_MAX, FLT_MAX);
    atlas.pixels = 0; atlas.width=atlas.height=0;atlas.line_height=16.0f;atlas.ascent=12.0f;atlas.tex_id = 0;atlas.white_uv=Vec2();
    memset(atlas.glyphs,0,sizeof(atlas.glyphs));memset(atlas.glyph_valid,0,sizeof(atlas.glyph_valid));
    theme_from = style;
    theme_target = style;
    theme_elapsed = 0.0f;
    theme_duration = 0.0f;
    theme_transitioning = false;
    theme_preset = Theme_Dark;
    ui_scale = 1.0f;
    nav_id = nav_activate_id = 0; nav_activate_key = Key_None;
    disabled_depth = 0; next_item_width = 0.0f; next_item_width_set = false;
    drag_drop_active = drag_drop_target = drag_drop_delivered = false;
    debug_log_callback = 0; debug_log_user_data = 0;
}

StreamingSeries::StreamingSeries(int capacity)
    : values_((size_t)Max((float)capacity, 1.0f)), head_(0), count_(0) {}
void StreamingSeries::Push(float value) {
    if (!FiniteFloat(value)) return;
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
Context* CreateContext() {
    Context* context = new Context();
    SetCurrentContext(context);
    return context;
}
void DestroyContext(Context* context) {
    if (!context) context = GOG;
    if (!context) return;
    for (size_t i = 0; i < context->windows.size(); i++) delete context->windows[i];
    CleanupContextState(context);
    if (GOG == context) GOG = 0;
    delete context;
}

static Window* FindWindow(const char* name) {
    ID id = HashLabelID(name, 0);
    for (size_t i = 0; i < GOG->windows.size(); i++)
        if (GOG->windows[i]->id == id) return GOG->windows[i];
    return 0;
}
static Window* CreateWindowObj(const char* name) {
    Window* w = new Window();
    w->name = name;
    w->id = HashLabelID(name, 0);
    // cascade newly created windows
    int n = (int)GOG->windows.size();
    w->pos  = Vec2(40.0f + n * 28.0f, 40.0f + n * 28.0f);
    w->size_full = Vec2(380, 420);
    w->focus_order = ++GOG->focus_counter;
    GOG->windows.push_back(w);
    return w;
}
static void FocusWindow(Window* w) {
    if (!(w->flags & WindowFlags_NoBringToFrontOnFocus)) w->focus_order = ++GOG->focus_counter;
    GOG->nav_window = w;
}

// =====================================================================
//  Frame lifecycle
// =====================================================================
enum InputModifierBits {
    InputMod_Shift = 1 << 0,
    InputMod_Ctrl  = 1 << 1,
    InputMod_Alt   = 1 << 2
};

static int ModifierMask(const bool key_state[Key_COUNT]) {
    int modifiers = 0;
    if (key_state[Key_Shift]) modifiers |= InputMod_Shift;
    if (key_state[Key_Ctrl]) modifiers |= InputMod_Ctrl;
    if (key_state[Key_Alt]) modifiers |= InputMod_Alt;
    return modifiers;
}

static void UpdateInputTransitions(Context& g) {
    bool key_state[Key_COUNT], key_seen[Key_COUNT];
    bool mouse_state[5], mouse_seen[5];
    g.focus_lost_this_frame = false;
    for (int key = 0; key < Key_COUNT; ++key) {
        key_state[key] = g.key_down_prev[key]; key_seen[key] = false;
        g.key_pressed[key] = false; g.key_consumed[key] = false; g.key_pressed_mods[key] = 0;
    }
    for (int button = 0; button < 5; ++button) {
        mouse_state[button] = g.mouse_down_prev[button]; mouse_seen[button] = false;
        g.mouse_clicked[button] = g.mouse_released[button] = false;
        g.mouse_clicked_mods[button] = g.mouse_released_mods[button] = 0;
    }

    // Replay queued transitions so a complete down/up pulse between two frames
    // remains observable even though IO::key_down/mouse_down hold only the final state.
    for (size_t i = 0; i < g.io.input_events.size(); ++i) {
        const InputEvent& event = g.io.input_events[i];
        if (event.type == InputEvent_Key && event.key_or_button > 0 && event.key_or_button < Key_COUNT) {
            int key = event.key_or_button; key_seen[key] = true;
            bool was_down = key_state[key];
            key_state[key] = event.down;
            if (event.down && !was_down && !g.key_pressed[key]) {
                g.key_pressed[key] = true;
                g.key_pressed_mods[key] = ModifierMask(key_state);
            }
        } else if (event.type == InputEvent_MouseButton && event.key_or_button >= 0 && event.key_or_button < 5) {
            int button = event.key_or_button; mouse_seen[button] = true;
            if (event.down && !mouse_state[button]) {
                g.mouse_clicked[button] = true;
                g.mouse_clicked_pos[button] = Vec2(event.x, event.y);
                g.mouse_clicked_mods[button] = ModifierMask(key_state);
            }
            if (!event.down && mouse_state[button]) {
                g.mouse_released[button] = true;
                g.mouse_released_pos[button] = Vec2(event.x, event.y);
                g.mouse_released_mods[button] = ModifierMask(key_state);
            }
            mouse_state[button] = event.down;
        } else if (event.type == InputEvent_Focus && !event.down) {
            g.focus_lost_this_frame = true;
            for (int key = 0; key < Key_COUNT; ++key) {
                key_state[key] = false; key_seen[key] = true; g.key_pressed[key] = false;
            }
            for (int button = 0; button < 5; ++button) {
                if (mouse_state[button]) {
                    g.mouse_released[button] = true;
                    g.mouse_released_pos[button] = g.io.mouse_pos;
                    g.mouse_released_mods[button] = ModifierMask(key_state);
                }
                mouse_state[button] = false; mouse_seen[button] = true; g.mouse_clicked[button] = false;
            }
        }
    }
    for (int key = 0; key < Key_COUNT; ++key) {
        if (!key_seen[key]) {
            g.key_pressed[key] = g.io.key_down[key] && !g.key_down_prev[key];
            if (g.key_pressed[key]) g.key_pressed_mods[key] = ModifierMask(g.io.key_down);
        }
    }
    for (int button = 0; button < 5; ++button) {
        if (!mouse_seen[button]) {
            g.mouse_clicked[button] = g.io.mouse_down[button] && !g.mouse_down_prev[button];
            g.mouse_released[button] = !g.io.mouse_down[button] && g.mouse_down_prev[button];
            if (g.mouse_clicked[button]) {
                g.mouse_clicked_pos[button] = g.io.mouse_pos;
                g.mouse_clicked_mods[button] = ModifierMask(g.io.key_down);
            }
            if (g.mouse_released[button]) {
                g.mouse_released_pos[button] = g.io.mouse_pos;
                g.mouse_released_mods[button] = ModifierMask(g.io.key_down);
            }
        }
    }
    if (!g.io.app_focused) {
        for (int key = 0; key < Key_COUNT; ++key) g.key_pressed[key] = false;
        for (int button = 0; button < 5; ++button) g.mouse_clicked[button] = false;
    }
}

void NewFrame() {
    if (!GOG) return;
    Context& g = *GOG;
    IO& io = g.io;
    if(!FiniteFloat(io.delta_time)||io.delta_time<=0.0f)io.delta_time=1.0f/60.0f;
    io.delta_time=Min(io.delta_time,0.25f);io.display_size.x=Max(io.display_size.x,0.0f);io.display_size.y=Max(io.display_size.y,0.0f);
    g.frame_count++;
    g.time += io.delta_time;
    g.metrics = FrameMetrics();
    g.metrics.frame_number = g.frame_count;
    g.metrics.input_events = (int)io.input_events.size();
    g.events.clear();
    g.last_item = LastItemData();
    g.drag_payload.preview = false; g.drag_payload.delivery = false;
    g.drag_drop_target = false; g.drag_drop_delivered = false;
    UpdateInputTransitions(g);
    if (g.focus_lost_this_frame) {
        g.active_id = g.text_active_id = 0;
        g.active_id_window = g.text_active_id_window = 0;
        g.moving_window = 0;
    }
    if (g.active_id && g.submitted_ids.find(g.active_id) == g.submitted_ids.end()) {
        g.active_id = 0; g.active_id_window = 0;
    }
    if (g.text_active_id) {
        std::map<ID, Vec4>::const_iterator active_text = g.submitted_ids.find(g.text_active_id);
        if (active_text == g.submitted_ids.end() ||
            (g.mouse_clicked[0] && !PointIn(g.mouse_clicked_pos[0], active_text->second))) {
            g.text_active_id = 0; g.text_active_id_window = 0;
        }
    }
    g.submitted_ids.clear();
    g.markdown_call_counts.clear();
    g.overlay_draw.Clear();
    g.overlay_draw.cur_tex = g.atlas.tex_id;
    g.overlay_draw.white_uv = g.atlas.white_uv;
    g.overlay_draw.PushClipRect(Vec4(0, 0, io.display_size.x, io.display_size.y));
    Window* modal_blocker = (g.modal_window && g.modal_window->active_this_frame) ? g.modal_window : 0;
    if (g.modal_window && !modal_blocker) g.modal_window = 0;

    // Keyboard transitions and the previous frame's focus order form a stable
    // immediate-mode navigation list. Widgets register themselves again below.
    for (int i = 0; i < Key_COUNT; ++i) {
        if (!io.key_down[i]) g.key_down_duration[i] = -1.0f;
        else if (g.key_pressed[i] || g.key_down_duration[i] < 0.0f) g.key_down_duration[i] = 0.0f;
        else g.key_down_duration[i] += io.delta_time;
    }
    g.nav_order_prev.clear(); g.nav_order_windows_prev.clear();
    for (size_t i = 0; i < g.nav_order.size(); ++i) {
        Window* owner = i < g.nav_order_windows.size() ? g.nav_order_windows[i] : 0;
        if (!modal_blocker || WindowInModalTree(g, owner)) {
            g.nav_order_prev.push_back(g.nav_order[i]); g.nav_order_windows_prev.push_back(owner);
        }
    }
    g.nav_order.clear(); g.nav_order_windows.clear();
    g.nav_activate_id = 0; g.nav_activate_key = Key_None;
    if(g.nav_id&&std::find(g.nav_order_prev.begin(),g.nav_order_prev.end(),g.nav_id)==g.nav_order_prev.end())g.nav_id=0;
    std::vector<int> tab_directions;
    bool nav_key_state[Key_COUNT];
    for (int key = 0; key < Key_COUNT; ++key) nav_key_state[key] = g.key_down_prev[key];
    for (size_t event_index = 0; event_index < io.input_events.size(); ++event_index) {
        const InputEvent& event = io.input_events[event_index];
        if (event.type == InputEvent_Key && event.key_or_button > 0 && event.key_or_button < Key_COUNT) {
            int key = event.key_or_button; bool was_down = nav_key_state[key]; nav_key_state[key] = event.down;
            if (key == Key_Tab && event.down && !was_down)
                tab_directions.push_back((ModifierMask(nav_key_state) & InputMod_Shift) ? -1 : 1);
        } else if (event.type == InputEvent_Focus && !event.down) {
            for (int key = 0; key < Key_COUNT; ++key) nav_key_state[key] = false;
        }
    }
    if (tab_directions.empty() && g.key_pressed[Key_Tab])
        tab_directions.push_back((g.key_pressed_mods[Key_Tab] & InputMod_Shift) ? -1 : 1);
    if (!tab_directions.empty() && !g.nav_order_prev.empty()) { // Tab / Shift+Tab
        g.text_active_id = 0; g.text_active_id_window = 0;
        int current = -1;
        for (size_t i = 0; i < g.nav_order_prev.size(); ++i)
            if (g.nav_order_prev[i] == g.nav_id) { current = (int)i; break; }
        int count = (int)g.nav_order_prev.size();
        for (size_t move = 0; move < tab_directions.size(); ++move) {
            int direction = tab_directions[move];
            current = (current < 0) ? (direction > 0 ? 0 : count - 1)
                                    : (current + direction + count) % count;
        }
        g.nav_id = g.nav_order_prev[(size_t)current];
    }
    if (!g.text_active_id && (g.key_pressed[Key_Enter] || g.key_pressed[Key_Space]) && g.nav_id) {
        g.nav_activate_id = g.nav_id;
        g.nav_activate_key = g.key_pressed[Key_Enter] ? Key_Enter : Key_Space;
    }

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
        g.style.disabled_alpha = g.theme_from.disabled_alpha + (g.theme_target.disabled_alpha - g.theme_from.disabled_alpha) * t;
        g.style.motion_scale = g.theme_from.motion_scale + (g.theme_target.motion_scale - g.theme_from.motion_scale) * t;
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

    if (!io.app_focused) {
        g.active_id = g.text_active_id = 0; g.active_id_window = g.text_active_id_window = 0; g.moving_window = 0;
    }

    g.hovered_id = 0;

    // A modal submitted on the previous frame owns pointer interaction until it
    // is closed or no longer submitted. Background active state cannot leak
    // through the modal barrier.
    if (modal_blocker && g.active_id_window && !WindowInModalTree(g, g.active_id_window)) {
        g.active_id = 0; g.active_id_window = 0;
    }
    if (modal_blocker && g.moving_window && !WindowInModalTree(g, g.moving_window)) g.moving_window = 0;

    // determine hovered window: topmost (highest focus_order) under the mouse
    g.hovered_window = 0;
    int best = -1;
    for (size_t i = 0; i < g.windows.size(); i++) {
        Window* w = g.windows[i];
        if (!w->active_this_frame) continue;            // not shown last frame
        if (modal_blocker && !WindowInModalTree(g, w)) continue;
        Vec4 r(w->pos.x, w->pos.y, w->pos.x + w->size.x, w->pos.y + w->size.y);
        if (PointIn(io.mouse_pos, r) && w->focus_order > best) { best = w->focus_order; g.hovered_window = w; }
    }
    if (modal_blocker && !g.hovered_window) g.hovered_window = modal_blocker;

    // active widget keeps its window "hovered" for drag continuation
    if (g.active_id && g.active_id_window && !g.drag_drop_active && (!modal_blocker || WindowInModalTree(g, g.active_id_window)))
        g.hovered_window = g.active_id_window;

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
    for (size_t i = 0; i < g.windows.size(); i++) {
        g.windows[i]->active_last_frame = g.windows[i]->active_this_frame;
        g.windows[i]->active_this_frame = false;
    }

    io.want_capture_mouse = (g.hovered_window != 0) || (g.active_id != 0) || g.drag_drop_active;
    io.want_capture_keyboard = (g.text_active_id != 0) || (g.nav_id != 0) || (modal_blocker != 0);
    io.want_text_input = g.text_active_id != 0;
}

void Render() {
    if (!GOG) return;
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
    if (g.modal_window && g.modal_window->active_this_frame) {
        std::vector<Window*> background, modal_descendants;
        for (size_t i = 0; i < order.size(); ++i) {
            if (!WindowInModalTree(g, order[i])) background.push_back(order[i]);
            else if (order[i] != g.modal_window) modal_descendants.push_back(order[i]);
        }
        background.push_back(g.modal_window);
        background.insert(background.end(), modal_descendants.begin(), modal_descendants.end());
        order.swap(background);
    }

    for (size_t i = 0; i < order.size(); i++) {
        DrawList* dl = &order[i]->draw;
        dl->CompactCommands();
        bool has_callback=false;for(size_t c=0;c<dl->cmds.size();++c)if(dl->cmds[c].callback){has_callback=true;break;}
        if (dl->idx.empty()&&!has_callback) continue;
        dd.lists.push_back(dl);
        dd.total_vtx += (int)dl->vtx.size();
        dd.total_idx += (int)dl->idx.size();
    }
    g.overlay_draw.CompactCommands();
    bool overlay_callback=false;for(size_t c=0;c<g.overlay_draw.cmds.size();++c)if(g.overlay_draw.cmds[c].callback){overlay_callback=true;break;}
    if (!g.overlay_draw.idx.empty()||overlay_callback) {
        dd.lists.push_back(&g.overlay_draw);
        dd.total_vtx += (int)g.overlay_draw.vtx.size();
        dd.total_idx += (int)g.overlay_draw.idx.size();
    }

    // store mouse for next-frame delta
    g.metrics.active_windows = (int)order.size();
    g.metrics.draw_lists = (int)dd.lists.size();
    g.metrics.vertices = dd.total_vtx; g.metrics.indices = dd.total_idx;
    g.metrics.ui_events = (int)g.events.size(); g.metrics.animation_states = (int)g.animations.size();
    for (size_t i = 0; i < dd.lists.size(); ++i) g.metrics.draw_commands += (int)dd.lists[i]->cmds.size();
    g.metrics_prev = g.metrics;

    for (int i = 0; i < 5; i++) g.mouse_down_prev[i] = g.io.mouse_down[i];
    for (int i = 0; i < Key_COUNT; ++i) g.key_down_prev[i] = g.io.key_down[i];
    g.mouse_pos_prev = g.io.mouse_pos;
    g.io.mouse_wheel = 0.0f;
    g.io.mouse_wheel_h = 0.0f;
    g.io.input_chars.clear();
    g.io.ClearInputEvents();
    if (g.drag_drop_active && !g.io.mouse_down[0]) {
        g.drag_drop_active = false; g.drag_drop_target = false; g.drag_drop_delivered = false;
        g.drag_payload_data.clear(); g.drag_payload_type.clear(); g.drag_payload = Payload();
    }
}

// =====================================================================
//  Text measuring
// =====================================================================
Vec2 CalcTextSize(const char* text, const char* text_end) {
    if (!GOG || !text) return Vec2();
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
    if (!GOG || !GOG->cur_window) return;
    Window* w = GOG->cur_window;
    Style& s = GOG->style;
    Vec2 item_pos = w->cursor;
    Vec4 item_rect(item_pos.x, item_pos.y, item_pos.x + Max(size.x, 0.0f), item_pos.y + Max(size.y, 0.0f));
    bool visible = true;
    if (!w->draw.clip_stack.empty()) {
        const Vec4& clip = w->draw.clip_stack.back();
        visible = item_rect.z > clip.x && item_rect.x < clip.z && item_rect.w > clip.y && item_rect.y < clip.w;
    }
    GOG->last_item.id = 0; GOG->last_item.rect = item_rect;
    GOG->last_item.status_flags = visible ? ItemStatus_Visible : ItemStatus_None;
    ++GOG->metrics.items_submitted;
    if (!visible) ++GOG->metrics.clipped_items;
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
    if (!GOG || !GOG->cur_window) return Vec2();
    Window* w = GOG->cur_window;
    float used = w->cursor.x - w->cursor_start.x;
    float avail_y = w->pos.y + w->size.y - GOG->style.window_padding.y - w->cursor.y;
    if (!w->draw.clip_stack.empty()) avail_y = w->draw.clip_stack.back().w - w->cursor.y;
    return Vec2(Max(0.0f, w->content_w - used), Max(0.0f, avail_y));
}
Vec2 GetCursorScreenPos() { return GOG && GOG->cur_window ? GOG->cur_window->cursor : Vec2(); }
void SetCursorScreenPos(const Vec2& pos) { if (GOG && GOG->cur_window) GOG->cur_window->cursor = pos; }
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
    GridStack().push_back(grid);

    w->id_stack.push_back(HashStr(id ? id : "grid", 0, w->id_stack.back()));
    w->cursor_start = Vec2(grid.outer_cursor.x, grid.row_y);
    w->cursor = w->cursor_start;
    w->indent = 0.0f;
    w->content_w = grid.column_width;
    return true;
}
void NextGridColumn() {
    if (GridStack().empty()) return;
    GridState& grid = GridStack().back();
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
    if (GridStack().empty()) return;
    GridState grid = GridStack().back();
    GridStack().pop_back();
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

struct ChildState {
    Window* window;
    ID id;
    Vec4 rect;
    int flags;
    Vec2 restore_cursor, restore_cursor_start, restore_cursor_prev, restore_cursor_max;
    float restore_curr_line, restore_prev_line, restore_indent, restore_content_w;
    Vec2 content_origin;
    int depth;
};
struct ChildScrollState { float y, max_y; ChildScrollState() : y(0), max_y(0) {} };
static thread_local std::map<Context*, std::vector<ChildState> > GChildStacks;
static thread_local std::map<Context*, std::map<ID, ChildScrollState> > GChildScrollStates;
static std::vector<ChildState>& ChildStack() { return GChildStacks[GOG]; }
static std::map<ID, ChildScrollState>& ChildScrollStates() { return GChildScrollStates[GOG]; }

bool BeginChild(const char* id, const Vec2& size_arg, int child_flags) {
    if (!GOG || !GOG->cur_window || !id) return false;
    Window* w = GOG->cur_window; Style& style = GOG->style;
    Vec2 available = GetContentRegionAvail();
    float width = size_arg.x > 0.0f ? size_arg.x : Max(1.0f, available.x + size_arg.x);
    float height = size_arg.y > 0.0f ? size_arg.y : Max(80.0f, available.y + size_arg.y);
    Vec2 pos = w->cursor; Vec4 rect(pos.x, pos.y, pos.x + width, pos.y + height);
    ID child_id = GetID(id);

    ItemSize(Vec2(width, height));
    ChildState child; child.window = w; child.id = child_id; child.rect = rect; child.flags = child_flags;
    child.depth = (int)ChildStack().size() + 1;
    child.restore_cursor = w->cursor; child.restore_cursor_start = w->cursor_start;
    child.restore_cursor_prev = w->cursor_prev_line; child.restore_cursor_max = w->cursor_max;
    child.restore_curr_line = w->curr_line_height; child.restore_prev_line = w->prev_line_height;
    child.restore_indent = w->indent; child.restore_content_w = w->content_w;

    ChildScrollState& scroll = ChildScrollStates()[child_id];
    bool hovered = GOG->hovered_window == w && PointIn(GOG->io.mouse_pos, rect);
    if (hovered && GOG->io.mouse_wheel != 0.0f && !(child_flags & ChildFlags_NoScrollbar) &&
        w->wheel_child_id == child_id && !w->wheel_child_consumed) {
        scroll.y = Clamp(scroll.y - GOG->io.mouse_wheel * 36.0f, 0.0f, scroll.max_y);
        w->wheel_child_consumed = true;
    }

    w->draw.AddRectFilledRounded(pos, Vec2(rect.z, rect.w), GetColorU32(Col_ChildBg), style.frame_rounding);
    if (child_flags & ChildFlags_Borders)
        w->draw.AddRect(pos, Vec2(rect.z, rect.w), GetColorU32(Col_Border), 1.0f);

    float padding_x = (child_flags & ChildFlags_AlwaysUseWindowPadding) ? style.window_padding.x : style.frame_padding.x;
    float padding_y = (child_flags & ChildFlags_AlwaysUseWindowPadding) ? style.window_padding.y : style.frame_padding.y;
    w->draw.PushClipRect(Vec4(rect.x + 1, rect.y + 1, rect.z - 1, rect.w - 1));
    w->id_stack.push_back(child_id);
    child.content_origin = Vec2(rect.x + padding_x, rect.y + padding_y - scroll.y);
    w->cursor_start = child.content_origin; w->cursor = child.content_origin;
    w->cursor_prev_line = w->cursor; w->cursor_max = w->cursor;
    w->curr_line_height = w->prev_line_height = 0.0f; w->indent = 0.0f;
    w->content_w = Max(1.0f, width - padding_x * 2.0f - ((child_flags & ChildFlags_NoScrollbar) ? 0.0f : style.scrollbar_size));
    ChildStack().push_back(child);
    return true;
}

void EndChild() {
    if (!GOG || ChildStack().empty()) return;
    ChildState child = ChildStack().back(); ChildStack().pop_back();
    Window* w = child.window; Style& style = GOG->style;
    float padding_y = (child.flags & ChildFlags_AlwaysUseWindowPadding) ? style.window_padding.y : style.frame_padding.y;
    float content_height = Max(0.0f, w->cursor_max.y - child.content_origin.y) + padding_y * 2.0f;
    float view_height = child.rect.w - child.rect.y;
    ChildScrollState& scroll = ChildScrollStates()[child.id];
    scroll.max_y = Max(0.0f, content_height - view_height);
    scroll.y = Clamp(scroll.y, 0.0f, scroll.max_y);
    if (!(child.flags & ChildFlags_NoScrollbar) && scroll.max_y > 0.0f)
        w->child_scroll_regions.push_back(ChildScrollRegion(child.rect, child.id, child.depth));
    w->draw.PopClipRect();
    if (w->id_stack.size() > 1) w->id_stack.pop_back();

    if (scroll.max_y > 0.0f && !(child.flags & ChildFlags_NoScrollbar)) {
        float track_x = child.rect.z - style.scrollbar_size;
        float track_h = child.rect.w - child.rect.y - 2.0f;
        float grab_h = Min(track_h, Max(style.grab_min_size, track_h * (view_height / Max(content_height, 1.0f))));
        float grab_range = Max(0.0f, track_h - grab_h);
        float track_y = child.rect.y + 1.0f;
        float grab_y = track_y + (scroll.y / scroll.max_y) * grab_range;
        Vec4 track(track_x, track_y, child.rect.z - 1, child.rect.w - 1);
        Vec4 grab(track_x + 2, grab_y, child.rect.z - 3, grab_y + grab_h);
        ID scrollbar_id = child.id ^ 0xC417D5C0A11B9E37ULL;
        GOG->submitted_ids[scrollbar_id] = track;
        bool track_hovered = GOG->hovered_window == w && PointIn(GOG->io.mouse_pos, track);
        bool grab_hovered = track_hovered && PointIn(GOG->io.mouse_pos, grab);
        const auto SetChildScrollFromPoint = [&](const Vec2& point) {
            float t = grab_range > 0.0f ? Clamp((point.y - track_y - GOG->active_id_click_offset.y) / grab_range, 0.0f, 1.0f) : 0.0f;
            scroll.y = t * scroll.max_y;
            grab_y = track_y + t * grab_range;
            grab = Vec4(track_x + 2, grab_y, child.rect.z - 3, grab_y + grab_h);
        };
        bool clicked_here = GOG->mouse_clicked[0] && (!GOG->active_id || GOG->active_id == scrollbar_id) &&
                            ItemContainsPoint(track, GOG->mouse_clicked_pos[0], w);
        if (clicked_here) {
            GOG->active_id = scrollbar_id; GOG->active_id_window = w;
            bool clicked_grab = PointIn(GOG->mouse_clicked_pos[0], grab);
            GOG->active_id_click_offset.y = clicked_grab ? GOG->mouse_clicked_pos[0].y - grab_y : grab_h * 0.5f;
            SetChildScrollFromPoint(GOG->mouse_clicked_pos[0]);
            FocusWindow(w);
        }
        bool held = false;
        if (GOG->active_id == scrollbar_id) {
            if (GOG->mouse_released[0]) {
                SetChildScrollFromPoint(GOG->mouse_released_pos[0]);
                GOG->active_id = 0; GOG->active_id_window = 0;
            } else if (GOG->io.mouse_down[0]) {
                held = true;
                SetChildScrollFromPoint(GOG->io.mouse_pos);
            } else { GOG->active_id = 0; GOG->active_id_window = 0; }
        }
        w->draw.AddRectFilled(Vec2(track.x, track.y), Vec2(track.z, track.w), GetColorU32(Col_ScrollbarBg));
        U32 grab_color = held ? GetColorU32(Col_FrameBgActive)
                         : (grab_hovered ? GetColorU32(Col_FrameBgHovered) : GetColorU32(Col_ScrollbarGrab));
        w->draw.AddRectFilledRounded(Vec2(grab.x, grab.y), Vec2(grab.z, grab.w), grab_color, 3.0f);
    }

    w->cursor = child.restore_cursor; w->cursor_start = child.restore_cursor_start;
    w->cursor_prev_line = child.restore_cursor_prev; w->cursor_max = child.restore_cursor_max;
    w->curr_line_height = child.restore_curr_line; w->prev_line_height = child.restore_prev_line;
    w->indent = child.restore_indent; w->content_w = child.restore_content_w;
    GOG->last_item.id = child.id; GOG->last_item.rect = child.rect;
    bool visible = true;
    if (!w->draw.clip_stack.empty()) {
        const Vec4& clip = w->draw.clip_stack.back();
        visible = child.rect.z > clip.x && child.rect.x < clip.z && child.rect.w > clip.y && child.rect.y < clip.w;
    }
    GOG->last_item.status_flags = visible ? ItemStatus_Visible : ItemStatus_None;
    if (visible && GOG->hovered_window == w && PointIn(GOG->io.mouse_pos, child.rect))
        GOG->last_item.status_flags |= ItemStatus_Hovered;
}

ListClipper::ListClipper() : display_start(0), display_end(0), item_count_(0), item_height_(0), start_y_(0), stepped_(false) {}
ListClipper::~ListClipper() { End(); }
void ListClipper::Begin(int item_count, float item_height) {
    End(); item_count_ = item_count > 0 ? item_count : 0; stepped_ = false;
    item_height_ = item_height > 0.0f ? item_height : (GOG ? GOG->atlas.line_height + GOG->style.item_spacing.y : 1.0f);
    start_y_ = GOG && GOG->cur_window ? GOG->cur_window->cursor.y : 0.0f;
    display_start = 0; display_end = item_count_;
    if (GOG && GOG->cur_window && !GOG->cur_window->draw.clip_stack.empty() && item_height_ > 0.0f) {
        const Vec4& clip = GOG->cur_window->draw.clip_stack.back();
        display_start = (int)Max(0.0f, floorf((clip.y - start_y_) / item_height_));
        display_end = (int)Min((float)item_count_, ceilf((clip.w - start_y_) / item_height_) + 1.0f);
    }
}
bool ListClipper::Step() {
    if (stepped_ || item_count_ <= 0 || !GOG || !GOG->cur_window) return false;
    stepped_ = true; GOG->cur_window->cursor.y = start_y_ + display_start * item_height_; return display_start < display_end;
}
void ListClipper::End() {
    if (item_count_ > 0 && GOG && GOG->cur_window) {
        Window* w = GOG->cur_window; w->cursor.y = start_y_ + item_count_ * item_height_;
        if (w->cursor.y > w->cursor_max.y) w->cursor_max.y = w->cursor.y;
    }
    item_count_ = 0; stepped_ = false;
}

static float CalcItemWidth() {
    Window* w = GOG->cur_window;
    if (GOG->next_item_width_set) {
        GOG->next_item_width_set = false;
        float width = GOG->next_item_width;
        return width < 0.0f ? Max(1.0f, GetContentRegionAvail().x + width) : Max(1.0f, width);
    }
    if (!GOG->item_width_stack.empty()) {
        float width = GOG->item_width_stack.back();
        return width < 0.0f ? Max(1.0f, GetContentRegionAvail().x + width) : Max(1.0f, width);
    }
    float fw = w->content_w * 0.62f;
    if (fw < 60) fw = 60;
    return fw;
}

// =====================================================================
//  Interaction
// =====================================================================
static Window* WindowAtPoint(const Vec2& point) {
    Context& g = *GOG;
    Window* result = 0;
    int best_focus_order = -1;
    for (size_t i = 0; i < g.windows.size(); ++i) {
        Window* candidate = g.windows[i];
        if (!candidate->active_last_frame && !candidate->active_this_frame) continue;
        if (!WindowInModalTree(g, candidate)) continue;
        Vec4 bounds(candidate->pos.x, candidate->pos.y,
                    candidate->pos.x + candidate->size.x, candidate->pos.y + candidate->size.y);
        if (PointIn(point, bounds) && candidate->focus_order > best_focus_order) {
            result = candidate;
            best_focus_order = candidate->focus_order;
        }
    }
    if (!result && g.modal_window && (g.modal_window->active_last_frame || g.modal_window->active_this_frame))
        return g.modal_window;
    return result;
}

static bool ItemContainsPoint(const Vec4& rect, const Vec2& point, Window* window) {
    if (!window || !WindowInModalTree(*GOG, window)) return false;
    if (WindowAtPoint(point) != window || !PointIn(point, rect)) return false;
    Vec4 body(window->pos.x, window->pos.y,
              window->pos.x + window->size.x, window->pos.y + window->size.y);
    if (!PointIn(point, body)) return false;
    return window->draw.clip_stack.empty() || PointIn(point, window->draw.clip_stack.back());
}

static bool ItemHoverable(const Vec4& r, ID id) {
    Context& g = *GOG;
    Window* w = g.cur_window;
    if (g.disabled_depth > 0) return false;
    if (!WindowInModalTree(g, w)) return false;
    if (g.hovered_window != w) return false;
    // A drag source retains the active ID, but drop targets must still be able
    // to become hovered while the pointer crosses them.
    if (g.active_id && g.active_id != id && !g.drag_drop_active) return false;
    if (!PointIn(g.io.mouse_pos, r)) return false;
    // clip against window body
    Vec4 body(w->pos.x, w->pos.y, w->pos.x + w->size.x, w->pos.y + w->size.y);
    if (!PointIn(g.io.mouse_pos, body)) return false;
    if (!w->draw.clip_stack.empty() && !PointIn(g.io.mouse_pos, w->draw.clip_stack.back())) return false;
    g.hovered_id = id;
    return true;
}
static void RegisterFocusable(ID id) {
    Context& g = *GOG;
    if (g.disabled_depth > 0 || !(g.last_item.status_flags & ItemStatus_Visible)) return;
    if (!WindowInModalTree(g, g.cur_window)) return;
    g.nav_order.push_back(id);
    g.nav_order_windows.push_back(g.cur_window);
    if (!g.nav_id) g.nav_id = id;
}
static void RestrictNavigationToWindow(Context& g, Window* owner) {
    std::vector<ID> ids; std::vector<Window*> owners;
    for (size_t i = 0; i < g.nav_order.size(); ++i) {
        Window* item_owner = i < g.nav_order_windows.size() ? g.nav_order_windows[i] : 0;
        if (item_owner == owner || (owner == g.modal_window && WindowInModalTree(g, item_owner))) {
            ids.push_back(g.nav_order[i]); owners.push_back(item_owner);
        }
    }
    g.nav_order.swap(ids); g.nav_order_windows.swap(owners);
    bool nav_owned = g.nav_id == 0;
    for (size_t i = 0; i < g.nav_order_prev.size() && !nav_owned; ++i)
        if (g.nav_order_prev[i] == g.nav_id && i < g.nav_order_windows_prev.size() &&
            (g.nav_order_windows_prev[i] == owner || (owner == g.modal_window && WindowInModalTree(g, g.nav_order_windows_prev[i])))) nav_owned = true;
    for (size_t i = 0; i < g.nav_order.size() && !nav_owned; ++i)
        if (g.nav_order[i] == g.nav_id) nav_owned = true;
    if (!nav_owned) g.nav_id = 0;
}
static bool ConsumeNavActivate(ID id) {
    if (!GOG || GOG->nav_activate_id != id) return false;
    GOG->nav_activate_id = 0;
    GOG->nav_activate_key = Key_None;
    return true;
}
static bool ButtonBehavior(const Vec4& r, ID id, bool* out_hovered, bool* out_held) {
    Context& g = *GOG;
    std::map<ID, Vec4>::iterator duplicate = g.submitted_ids.find(id);
    if (duplicate != g.submitted_ids.end()) ++g.metrics.id_conflicts;
    else g.submitted_ids[id] = r;
    g.last_item.id = id; g.last_item.rect = r;
    g.last_item.status_flags &= ItemStatus_Visible;
    if (g.disabled_depth > 0) g.last_item.status_flags |= ItemStatus_Disabled;
    RegisterFocusable(id);
    bool hovered = ItemHoverable(r, id);
    bool pressed = false, held = false;
    bool clicked_here = g.mouse_clicked[0] && g.disabled_depth == 0 &&
                        (!g.active_id || g.active_id == id) &&
                        ItemContainsPoint(r, g.mouse_clicked_pos[0], g.cur_window);
    if (clicked_here) {
        g.active_id = id; g.active_id_window = g.cur_window; g.nav_id = id;
        g.text_active_id = 0; g.text_active_id_window = 0; FocusWindow(g.cur_window);
    }
    if (g.active_id == id && (g.disabled_depth > 0 || !(g.last_item.status_flags & ItemStatus_Visible) ||
                              !WindowInModalTree(g, g.cur_window))) {
        g.active_id = 0; g.active_id_window = 0;
    }
    if (g.active_id == id) {
        if (g.mouse_released[0]) {
            if (ItemContainsPoint(r, g.mouse_released_pos[0], g.cur_window)) pressed = true;
            g.active_id = 0; g.active_id_window = 0;
        } else if (g.io.mouse_down[0]) held = true;
        else {
            if (hovered) pressed = true;
            g.active_id = 0; g.active_id_window = 0;
        }
    }
    if (g.disabled_depth == 0 && WindowInModalTree(g, g.cur_window) &&
        (g.last_item.status_flags & ItemStatus_Visible) && ConsumeNavActivate(id)) pressed = true;
    if (hovered) g.last_item.status_flags |= ItemStatus_Hovered;
    if (g.active_id == id || held) g.last_item.status_flags |= ItemStatus_Active;
    if (g.nav_id == id) g.last_item.status_flags |= ItemStatus_Focused;
    if (pressed) g.last_item.status_flags |= ItemStatus_Clicked;
    if (g.disabled_depth == 0 && g.nav_id == id && (g.last_item.status_flags & ItemStatus_Visible))
        g.cur_window->draw.AddRect(Vec2(r.x - 1.0f, r.y - 1.0f), Vec2(r.z + 1.0f, r.w + 1.0f),
                                   GetColorU32(Col_FocusRing), 2.0f);
    if (out_hovered) *out_hovered = hovered;
    if (out_held)    *out_held = held;
    return pressed;
}
static bool WindowChromeButton(const Vec4& rect, ID id, Window* window, bool* out_hovered) {
    Context& g = *GOG;
    g.submitted_ids[id] = rect;
    bool hovered = g.hovered_window == window && PointIn(g.io.mouse_pos, rect) && WindowInModalTree(g, window);
    bool clicked_here = g.mouse_clicked[0] && (!g.active_id || g.active_id == id) &&
                        ItemContainsPoint(rect, g.mouse_clicked_pos[0], window);
    if (clicked_here) {
        g.active_id = id; g.active_id_window = window; FocusWindow(window);
    }
    bool pressed = false;
    if (g.active_id == id) {
        if (!WindowInModalTree(g, window)) {
            g.active_id = 0; g.active_id_window = 0;
        } else if (g.mouse_released[0]) {
            pressed = ItemContainsPoint(rect, g.mouse_released_pos[0], window);
            g.active_id = 0; g.active_id_window = 0;
        } else if (!g.io.mouse_down[0]) {
            g.active_id = 0; g.active_id_window = 0;
        }
    }
    if (out_hovered) *out_hovered = hovered;
    return pressed;
}
static void MarkItemEdited(ID id) {
    if (GOG && GOG->last_item.id == id) GOG->last_item.status_flags |= ItemStatus_Edited;
}
bool IsItemHovered() { return GOG && (GOG->last_item.status_flags & ItemStatus_Hovered) != 0; }
bool IsItemActive() { return GOG && (GOG->last_item.status_flags & ItemStatus_Active) != 0; }
bool IsItemFocused() { return GOG && (GOG->last_item.status_flags & ItemStatus_Focused) != 0; }
bool IsItemClicked(int mouse_button) {
    return GOG && GOG->cur_window && mouse_button >= 0 && mouse_button < 5 &&
           GOG->last_item.id != 0 && (GOG->last_item.status_flags & ItemStatus_Visible) &&
           !(GOG->last_item.status_flags & ItemStatus_Disabled) &&
           GOG->mouse_clicked[mouse_button] &&
           ItemContainsPoint(GOG->last_item.rect, GOG->mouse_clicked_pos[mouse_button], GOG->cur_window);
}
bool IsItemEdited() { return GOG && (GOG->last_item.status_flags & ItemStatus_Edited) != 0; }
bool IsItemVisible() { return GOG && (GOG->last_item.status_flags & ItemStatus_Visible) != 0; }
ID GetItemID() { return GOG ? GOG->last_item.id : 0; }
Vec2 GetItemRectMin() { return GOG ? Vec2(GOG->last_item.rect.x, GOG->last_item.rect.y) : Vec2(); }
Vec2 GetItemRectMax() { return GOG ? Vec2(GOG->last_item.rect.z, GOG->last_item.rect.w) : Vec2(); }
Vec2 GetItemRectSize() { return GOG ? Vec2(GOG->last_item.rect.z-GOG->last_item.rect.x, GOG->last_item.rect.w-GOG->last_item.rect.y) : Vec2(); }
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
void SetNextWindowPos(const Vec2& pos, int cond)   { if (GOG) { GOG->next_pos_set = true; GOG->next_pos = pos; GOG->next_pos_cond = cond; } }
void SetNextWindowSize(const Vec2& size, int cond) { if (GOG) { GOG->next_size_set = true; GOG->next_size = size; GOG->next_size_cond = cond; } }
void SetNextWindowSizeConstraints(const Vec2& min_size, const Vec2& max_size) {
    if (!GOG) return; GOG->next_constraints_set = true; GOG->next_size_min = min_size; GOG->next_size_max = max_size;
}
void SetNextWindowDock(DockSlot slot) { if (GOG) { NextDockSlot() = slot; NextDockSet() = true; } }
void DockWindow(const char* name, DockSlot slot) {
    Window* w = name ? FindWindow(name) : 0;
    if (w) w->dock_slot = slot;
}
Vec2 GetWindowPos() { return GOG && GOG->cur_window ? GOG->cur_window->pos : Vec2(); }
Vec2 GetWindowSize() { return GOG && GOG->cur_window ? GOG->cur_window->size : Vec2(); }
float GetWindowScrollY() { return GOG && GOG->cur_window ? GOG->cur_window->scroll_y : 0.0f; }
float GetWindowScrollMaxY() { return GOG && GOG->cur_window ? GOG->cur_window->scroll_max_y : 0.0f; }
void SetWindowPos(const Vec2& pos) { if (GOG && GOG->cur_window) { GOG->cur_window->pos = pos; GOG->cur_window->dock_slot = Dock_None; } }
void SetWindowSize(const Vec2& size) { if (GOG && GOG->cur_window) GOG->cur_window->size_full = Vec2(Max(size.x,120.0f),Max(size.y,40.0f)); }
void SetWindowScrollY(float scroll_y) { if (GOG && GOG->cur_window) GOG->cur_window->scroll_y = Clamp(scroll_y,0.0f,GOG->cur_window->scroll_max_y); }
void SetWindowCollapsed(bool collapsed) { if (GOG && GOG->cur_window && !(GOG->cur_window->flags & WindowFlags_NoCollapse)) GOG->cur_window->collapsed = collapsed; }

static bool ShouldApplyCondition(int cond, bool created, bool appearing) {
    if (cond == Cond_None || (cond & Cond_Always)) return true;
    if ((cond & (Cond_Once | Cond_FirstUseEver)) && created) return true;
    return (cond & Cond_Appearing) && appearing;
}

bool Begin(const char* name, bool* p_open, int flags) {
    if (!GOG || !name || !*name) return false;
    Context& g = *GOG;
    Style& s = g.style;
    Window* w = FindWindow(name);
    bool created = false;
    if (!w) { w = CreateWindowObj(name); created = true; }
    else w->name = name; // dynamic visible titles may retain a stable ### identity
    g.window_parent_stack.push_back(g.cur_window);
    bool appearing = !w->active_last_frame;
    if (g.next_pos_set && ShouldApplyCondition(g.next_pos_cond, created, appearing)) w->pos = g.next_pos;
    if (g.next_size_set && ShouldApplyCondition(g.next_size_cond, created, appearing)) w->size_full = g.next_size;
    if (g.next_constraints_set) {
        w->size_full.x = Clamp(w->size_full.x, Max(g.next_size_min.x, 1.0f), Max(g.next_size_max.x, g.next_size_min.x));
        w->size_full.y = Clamp(w->size_full.y, Max(g.next_size_min.y, 1.0f), Max(g.next_size_max.y, g.next_size_min.y));
    }
    if (NextDockSet()) w->dock_slot = NextDockSlot();
    g.next_pos_set = g.next_size_set = false;
    g.next_constraints_set = false;
    NextDockSet() = false;
    w->flags = flags; w->appeared_this_frame = appearing; w->skip_items = false;

    if (p_open && !*p_open) {
        g.cur_window = w; w->active_this_frame = false; w->skip_items = true; w->draw.Clear();
        w->id_stack.clear(); w->id_stack.push_back(w->id);
        return false;
    }
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

    float title_h = (flags & WindowFlags_NoTitleBar) ? 0.0f : s.window_title_height;

    // Child hit boxes are retained for one frame so wheel routing is known
    // before the immediate-mode child calls are submitted again.
    w->wheel_child_id = 0; w->wheel_child_consumed = false; int deepest_child = -1;
    for (size_t i = 0; i < w->child_scroll_regions.size(); ++i) {
        const ChildScrollRegion& region = w->child_scroll_regions[i];
        if (PointIn(g.io.mouse_pos, region.rect) && region.depth >= deepest_child) {
            deepest_child = region.depth; w->wheel_child_id = region.id;
        }
    }
    bool child_owns_wheel = w->wheel_child_id != 0;
    w->child_scroll_regions.clear();
    if (g.hovered_window == w && g.io.mouse_wheel != 0 && !w->collapsed && !child_owns_wheel)
        w->scroll_y -= g.io.mouse_wheel * 36.0f;
    w->scroll_y = Clamp(w->scroll_y, 0.0f, w->scroll_max_y);

    Vec2 pos = w->pos;
    if (flags & (WindowFlags_NoCollapse | WindowFlags_NoTitleBar)) w->collapsed = false;
    Vec2 size = w->collapsed ? Vec2(w->size_full.x, title_h) : w->size_full;
    w->size = size;
    if (WindowInModalTree(g, w) && PointIn(g.io.mouse_pos, Vec4(pos.x, pos.y, pos.x + size.x, pos.y + size.y)) &&
        (!g.hovered_window || w->focus_order >= g.hovered_window->focus_order)) g.hovered_window = w;
    DrawList* dl = &w->draw;
    bool focused = (g.nav_window == w);

    if (flags & WindowFlags_InternalModal)
        dl->AddRectFilled(Vec2(0, 0), g.io.display_size, GetColorU32(Col_ModalDim));
    dl->PushClipRect(Vec4(pos.x - s.shadow_size - 2, pos.y - s.shadow_size - 2,
                          pos.x + size.x + s.shadow_size + 2, pos.y + size.y + s.shadow_size + 2));

    if (!(flags & WindowFlags_NoBackground)) {
        dl->AddShadowRect(pos, Vec2(pos.x + size.x, pos.y + size.y),
                          GetColorU32(Col_WindowShadow), s.window_rounding, s.shadow_size);
        if (!w->collapsed)
            dl->AddRectFilledRounded(pos, Vec2(pos.x + size.x, pos.y + size.y), GetColorU32(Col_WindowBg), s.window_rounding);
        if (title_h > 0.0f) {
            dl->AddRectFilledRounded(pos, Vec2(pos.x + size.x, pos.y + title_h + 8), GetColorU32(focused ? Col_TitleBgActive : Col_TitleBg), s.window_rounding);
            dl->AddRectFilled(Vec2(pos.x, pos.y + title_h), Vec2(pos.x + size.x, pos.y + title_h + 8), GetColorU32(Col_WindowBg));
            dl->AddRectFilledMultiColor(Vec2(pos.x + s.window_rounding, pos.y + title_h - 2),
                                        Vec2(pos.x + size.x - s.window_rounding, pos.y + title_h),
                                        GetColorU32(Col_GradientStart), GetColorU32(Col_GradientEnd),
                                        GetColorU32(Col_GradientEnd), GetColorU32(Col_GradientStart));
            dl->AddCircleFilled(Vec2(pos.x + 18, pos.y + title_h * 0.5f), 4.0f, GetColorU32(Col_CheckMark), 16);
        }
    }

    // collapse arrow
    float asz = g.atlas.line_height * 0.7f;
    if (title_h > 0.0f && !(flags & WindowFlags_NoCollapse))
        RenderArrow(dl, Vec2(pos.x + 30, pos.y + (title_h - asz) * 0.5f), asz, GetColorU32(Col_TextDisabled), w->collapsed ? 0 : 1);
    Vec4 arrow_rect(pos.x, pos.y, pos.x + 52, pos.y + title_h);

    // title text
    const char* disp_end = FindDisplayEnd(name);
    if (title_h > 0.0f) dl->AddText(Vec2(pos.x + title_h + 18, pos.y + s.frame_padding.y), GetColorU32(Col_Text), name, disp_end);

    // close button
    bool close_clicked = false;
    bool close_hovered = false;
    Vec4 close_rect;
    if (p_open && title_h > 0.0f) {
        close_rect = Vec4(pos.x + size.x - title_h, pos.y, pos.x + size.x, pos.y + title_h);
        close_clicked = WindowChromeButton(close_rect, w->id ^ 0xC105EB07704EULL, w, &close_hovered);
        if (close_hovered) dl->AddRectFilled(Vec2(close_rect.x, close_rect.y), Vec2(close_rect.z, close_rect.w), GetColorU32(Col_ButtonHovered));
        Vec2 cc((close_rect.x + close_rect.z) * 0.5f, (close_rect.y + close_rect.w) * 0.5f);
        float rr = 3.5f;
        dl->AddLine(Vec2(cc.x - rr, cc.y - rr), Vec2(cc.x + rr, cc.y + rr), GetColorU32(Col_Text), 1.0f);
        dl->AddLine(Vec2(cc.x + rr, cc.y - rr), Vec2(cc.x - rr, cc.y + rr), GetColorU32(Col_Text), 1.0f);
    }

    if (!(flags & WindowFlags_NoBackground)) dl->AddRect(pos, Vec2(pos.x + size.x, pos.y + size.y), GetColorU32(Col_Border), 1.0f);

    // -------- interactions --------
    Vec4 title_rect(pos.x, pos.y, pos.x + size.x, pos.y + title_h);
    bool arrow_clicked = title_h > 0.0f && !(flags & WindowFlags_NoCollapse) &&
                         WindowChromeButton(arrow_rect, w->id ^ 0xC011A953AA0ULL, w, 0);
    bool click_in_window = g.mouse_clicked[0] && WindowAtPoint(g.mouse_clicked_pos[0]) == w &&
                           PointIn(g.mouse_clicked_pos[0], Vec4(pos.x, pos.y, pos.x + size.x, pos.y + size.y));
    if (click_in_window) FocusWindow(w);

    if (arrow_clicked) {
        w->collapsed = !w->collapsed;
    } else if (title_h > 0.0f && !(flags & WindowFlags_NoMove) && !close_clicked &&
               g.mouse_clicked[0] && !g.active_id && ItemContainsPoint(title_rect, g.mouse_clicked_pos[0], w) &&
               !PointIn(g.mouse_clicked_pos[0], arrow_rect) && (!p_open || !PointIn(g.mouse_clicked_pos[0], close_rect))) {
        w->dock_slot = Dock_None;
        g.moving_window = w; g.active_id = 0;
    }
    if (close_clicked && p_open) {
        *p_open = false;
        g.events.push_back(Event(Event_WindowClosed, w->id, name));
    }

    // -------- layout setup --------
    w->scrollbar_active = (!w->collapsed && !(flags & WindowFlags_NoScrollbar) && w->scroll_max_y > 0.0f);
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
    if (!GOG || !GOG->cur_window) return;
    Context& g = *GOG;
    Style& s = g.style;
    Window* w = g.cur_window;
    if (w->skip_items) {
        w->skip_items = false;
        g.cur_window = g.window_parent_stack.empty() ? 0 : g.window_parent_stack.back();
        if (!g.window_parent_stack.empty()) g.window_parent_stack.pop_back();
        return;
    }
    DrawList* dl = &w->draw;
    float title_h = (w->flags & WindowFlags_NoTitleBar) ? 0.0f : s.window_title_height;

    dl->PopClipRect();  // content clip

    // content height / scroll range
    float content_h = (w->cursor_max.y - w->cursor_start.y) + s.window_padding.y;
    float avail = w->size.y - title_h - s.window_padding.y;
    w->scroll_max_y = (content_h > avail) ? (content_h - avail) : 0.0f;

    if (!w->collapsed) {
        Vec2 pos = w->pos, size = w->size;

        // scrollbar
        if (w->scroll_max_y > 0.0f && !(w->flags & WindowFlags_NoScrollbar)) {
            float track_x0 = pos.x + size.x - s.scrollbar_size;
            float track_y0 = pos.y + title_h;
            float track_y1 = pos.y + size.y;
            float track_h = track_y1 - track_y0;
            float grab_h = Min(track_h, Max(s.grab_min_size, track_h * (avail / content_h)));
            float grab_range = Max(0.0f, track_h - grab_h);
            float grab_y = track_y0 + (w->scroll_y / w->scroll_max_y) * grab_range;
            Vec4 track(track_x0, track_y0, pos.x + size.x, track_y1);
            Vec4 grab(track_x0 + 2, grab_y, pos.x + size.x - 2, grab_y + grab_h);
            ID scrollbar_id = w->id ^ 0x53C0A11B4A2D19E7ULL;
            g.submitted_ids[scrollbar_id] = track;
            bool track_hovered = g.hovered_window == w && PointIn(g.io.mouse_pos, track);
            bool grab_hovered = track_hovered && PointIn(g.io.mouse_pos, grab);
            const auto SetWindowScrollFromPoint = [&](const Vec2& point) {
                float t = grab_range > 0.0f ? Clamp((point.y - track_y0 - g.active_id_click_offset.y) / grab_range, 0.0f, 1.0f) : 0.0f;
                w->scroll_y = t * w->scroll_max_y;
                grab_y = track_y0 + t * grab_range;
                grab = Vec4(track_x0 + 2, grab_y, pos.x + size.x - 2, grab_y + grab_h);
            };
            bool clicked_here = g.mouse_clicked[0] && (!g.active_id || g.active_id == scrollbar_id) &&
                                ItemContainsPoint(track, g.mouse_clicked_pos[0], w);
            if (clicked_here) {
                g.active_id = scrollbar_id; g.active_id_window = w; FocusWindow(w);
                bool clicked_grab = PointIn(g.mouse_clicked_pos[0], grab);
                g.active_id_click_offset.y = clicked_grab ? g.mouse_clicked_pos[0].y - grab_y : grab_h * 0.5f;
                SetWindowScrollFromPoint(g.mouse_clicked_pos[0]);
            }
            bool scrollbar_held = false;
            if (g.active_id == scrollbar_id) {
                if (g.mouse_released[0]) {
                    SetWindowScrollFromPoint(g.mouse_released_pos[0]);
                    g.active_id = 0; g.active_id_window = 0;
                } else if (g.io.mouse_down[0]) {
                    scrollbar_held = true;
                    SetWindowScrollFromPoint(g.io.mouse_pos);
                } else { g.active_id = 0; g.active_id_window = 0; }
            }
            dl->AddRectFilled(Vec2(track.x, track.y), Vec2(track.z, track.w), GetColorU32(Col_ScrollbarBg));
            U32 grab_color = scrollbar_held ? GetColorU32(Col_FrameBgActive)
                             : (grab_hovered ? GetColorU32(Col_FrameBgHovered) : GetColorU32(Col_ScrollbarGrab));
            dl->AddRectFilledRounded(Vec2(grab.x, grab.y), Vec2(grab.z, grab.w), grab_color, s.frame_rounding * 0.5f);
        }

        // resize grip
        Vec4 grip(pos.x + size.x - 16, pos.y + size.y - 16, pos.x + size.x, pos.y + size.y);
        ID gid = w->id ^ 0x9E3779B97F4A7C15ULL;
        if (!(w->flags & WindowFlags_NoResize)) g.submitted_ids[gid] = grip;
        bool ghov = (g.hovered_window == w) && PointIn(g.io.mouse_pos, grip);
        bool held = false;
        bool resize_clicked = !(w->flags & WindowFlags_NoResize) && g.mouse_clicked[0] &&
                              (!g.active_id || g.active_id == gid) &&
                              ItemContainsPoint(grip, g.mouse_clicked_pos[0], w);
        if (resize_clicked) {
            w->dock_slot = Dock_None;
            g.active_id = gid; g.active_id_window = w;
            g.active_id_click_offset = Vec2(g.mouse_clicked_pos[0].x - (pos.x + size.x),
                                            g.mouse_clicked_pos[0].y - (pos.y + size.y));
            FocusWindow(w);
        }
        if (!(w->flags & WindowFlags_NoResize) && g.active_id == gid) {
            if (g.mouse_released[0]) {
                w->size_full.x = Max(160.0f, g.mouse_released_pos[0].x - pos.x - g.active_id_click_offset.x);
                w->size_full.y = Max(60.0f, g.mouse_released_pos[0].y - pos.y - g.active_id_click_offset.y);
                g.active_id = 0; g.active_id_window = 0;
            } else if (g.io.mouse_down[0]) {
                held = true;
                w->size_full.x = Max(160.0f, g.io.mouse_pos.x - pos.x - g.active_id_click_offset.x);
                w->size_full.y = Max(60.0f,  g.io.mouse_pos.y - pos.y - g.active_id_click_offset.y);
            } else { g.active_id = 0; g.active_id_window = 0; }
        }
        if (!(w->flags & WindowFlags_NoResize)) {
            U32 gc = GetColorU32(held ? Col_ResizeGripActive : (ghov ? Col_ResizeGripHovered : Col_ResizeGrip));
            dl->AddTriangleFilled(Vec2(grip.z, grip.w), Vec2(grip.z, grip.w - 14), Vec2(grip.z - 14, grip.w), gc);
        }
    }

    if (w->flags & WindowFlags_AlwaysAutoResize) {
        w->size_full.x = Max(120.0f, w->cursor_max.x - w->pos.x + s.window_padding.x);
        w->size_full.y = Max(title_h + 40.0f, content_h + title_h + s.window_padding.y);
    }

    dl->PopClipRect();  // window clip
    g.cur_window = g.window_parent_stack.empty() ? 0 : g.window_parent_stack.back();
    if (!g.window_parent_stack.empty()) g.window_parent_stack.pop_back();
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
void TextUnformatted(const char* text, const char* text_end) {
    if (!text || !GOG || !GOG->cur_window) return; TextImpl(text, text_end, GetColorU32(Col_Text));
}
static void WrappedTextImpl(const char* text, U32 color) {
    if (!text || !GOG || !GOG->cur_window) return;
    std::string source(text), line;
    size_t start = 0;
    while (start < source.size()) {
        size_t newline = source.find('\n', start);
        size_t segment_end = newline == std::string::npos ? source.size() : newline;
        size_t cursor = start;
        while (cursor < segment_end) {
            while (cursor < segment_end && source[cursor] == ' ') ++cursor;
            size_t word_end = source.find(' ', cursor); if (word_end == std::string::npos || word_end > segment_end) word_end = segment_end;
            std::string word = source.substr(cursor, word_end - cursor);
            std::string candidate = line.empty() ? word : line + " " + word;
            if (!line.empty() && CalcTextSize(candidate.c_str()).x > GetContentRegionAvail().x) { TextImpl(line.c_str(), 0, color); line = word; }
            else line = candidate;
            cursor = word_end + 1;
        }
        if (!line.empty()) { TextImpl(line.c_str(), 0, color); line.clear(); }
        if (newline == std::string::npos) break;
        start = newline + 1;
        if (start == source.size()) ItemSize(Vec2(0, GOG->atlas.line_height));
    }
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
void TextWrapped(const char* fmt, ...) {
    char buffer[2048]; va_list args; va_start(args, fmt); vsnprintf(buffer, sizeof(buffer), fmt, args); va_end(args);
    WrappedTextImpl(buffer, GetColorU32(Col_Text));
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
    Vec2 size((size_arg.x > 0 ? size_arg.x : (size_arg.x < 0 ? Max(1.0f, GetContentRegionAvail().x + size_arg.x) : ls.x + s.frame_padding.x * 2)),
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
bool InvisibleButton(const char* label, const Vec2& size) {
    if (!GOG || !GOG->cur_window || !label || size.x <= 0.0f || size.y <= 0.0f) return false;
    Window* w = GOG->cur_window; Vec2 pos = w->cursor; Vec4 rect(pos.x,pos.y,pos.x+size.x,pos.y+size.y);
    ItemSize(size); bool hovered=false, held=false; return ButtonBehavior(rect, GetID(label), &hovered, &held);
}
bool Selectable(const char* label, bool selected, const Vec2& size_arg) {
    if (!GOG || !GOG->cur_window || !label) return false;
    Window* w=GOG->cur_window; Style& s=GOG->style; ID id=GetID(label); const char* end=FindDisplayEnd(label);
    Vec2 text_size=CalcTextSize(label,end); float width=size_arg.x>0?size_arg.x:(size_arg.x<0?Max(1.0f,GetContentRegionAvail().x+size_arg.x):GetContentRegionAvail().x);
    float height=size_arg.y>0?size_arg.y:text_size.y+s.frame_padding.y*2; Vec2 pos=w->cursor; Vec4 rect(pos.x,pos.y,pos.x+width,pos.y+height);
    ItemSize(Vec2(width,height)); bool hovered=false,held=false; bool pressed=ButtonBehavior(rect,id,&hovered,&held);
    if(selected||hovered) w->draw.AddRectFilledRounded(pos,Vec2(rect.z,rect.w),GetColorU32(selected?Col_HeaderActive:Col_HeaderHovered),s.frame_rounding);
    w->draw.AddText(Vec2(pos.x+s.frame_padding.x,pos.y+s.frame_padding.y),GetColorU32(Col_Text),label,end);
    if(pressed) GOG->events.push_back(Event(Event_Clicked,id,label)); return pressed;
}

bool Checkbox(const char* label, bool* v) {
    Window* w = GOG->cur_window;
    Style& s = GOG->style;
    ID id = GetID(label);
    const char* end = FindDisplayEnd(label);
    float sq = GOG->atlas.line_height + s.frame_padding.y * 2;
    float pill_h = sq * 0.72f, pill_w = sq * 1.35f;
    Vec2 ls = CalcTextSize(label, end);
    Vec2 pos = w->cursor;
    Vec2 total(pill_w + s.item_inner_spacing.x + ls.x, sq);
    Vec4 box(pos.x, pos.y, pos.x + sq, pos.y + sq);
    ItemSize(total);
    bool hovered, held;
    bool pressed = ButtonBehavior(Vec4(pos.x, pos.y, pos.x + total.x, pos.y + total.y), id, &hovered, &held);
    if (pressed) {
        *v = !*v;
        MarkItemEdited(id);
        GOG->events.push_back(Event(Event_ValueChanged, id, label));
    }
    U32 bg = GetColorU32(held ? Col_FrameBgActive : (hovered ? Col_FrameBgHovered : Col_FrameBg));
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
    if (pressed && *v != v_button) {
        *v = v_button;
        MarkItemEdited(id);
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
    if (g.submitted_ids.find(id) != g.submitted_ids.end()) ++g.metrics.id_conflicts;
    else g.submitted_ids[id] = r;
    g.last_item.id = id; g.last_item.rect = r; g.last_item.status_flags &= ItemStatus_Visible;
    if (g.disabled_depth > 0) g.last_item.status_flags |= ItemStatus_Disabled;
    RegisterFocusable(id);
    bool hovered = ItemHoverable(r, id);
    bool changed = false, held = false;
    const auto SetValueFromPoint = [&](const Vec2& point) {
        float grab = GOG->style.grab_min_size;
        float usable = (r.z - r.x) - grab;
        float t = usable > 0.0f ? Clamp((point.x - r.x - grab * 0.5f) / usable, 0.0f, 1.0f) : 0.0f;
        float next = v_min + t * (v_max - v_min);
        if (next != *v) { *v = next; changed = true; }
    };
    bool clicked_here = g.mouse_clicked[0] && g.disabled_depth == 0 &&
                        (!g.active_id || g.active_id == id) &&
                        ItemContainsPoint(r, g.mouse_clicked_pos[0], g.cur_window);
    if (clicked_here) {
        g.active_id = id; g.active_id_window = g.cur_window; g.nav_id = id;
        g.text_active_id = 0; g.text_active_id_window = 0; FocusWindow(g.cur_window);
        SetValueFromPoint(g.mouse_clicked_pos[0]);
    }
    if (g.active_id == id) {
        if (g.disabled_depth > 0) {
            g.active_id = 0; g.active_id_window = 0;
        } else if (g.mouse_released[0]) {
            SetValueFromPoint(g.mouse_released_pos[0]);
            g.active_id = 0; g.active_id_window = 0;
        } else if (g.io.mouse_down[0]) {
            held = true;
            SetValueFromPoint(g.io.mouse_pos);
        } else { g.active_id = 0; g.active_id_window = 0; }
    }
    if (g.disabled_depth == 0 && WindowInModalTree(g, g.cur_window) &&
        (g.last_item.status_flags & ItemStatus_Visible) && g.nav_id == id) {
        float step = (v_max - v_min) / 100.0f;
        if (step <= 0.0f) step = 1.0f;
        float next = *v;
        if (g.key_pressed[Key_LeftArrow] || g.key_pressed[Key_DownArrow]) next = Clamp(next - step, v_min, v_max);
        if (g.key_pressed[Key_UpArrow] || g.key_pressed[Key_RightArrow]) next = Clamp(next + step, v_min, v_max);
        if (next != *v) { *v = next; changed = true; }
    }
    if (out_held) *out_held = held || (g.active_id == id);
    if (hovered) g.last_item.status_flags |= ItemStatus_Hovered;
    if (held || g.active_id == id) g.last_item.status_flags |= ItemStatus_Active;
    if (g.nav_id == id) g.last_item.status_flags |= ItemStatus_Focused;
    if (changed) g.last_item.status_flags |= ItemStatus_Edited;
    if (g.disabled_depth == 0 && g.nav_id == id && (g.last_item.status_flags & ItemStatus_Visible))
        g.cur_window->draw.AddRect(Vec2(r.x - 1.0f, r.y - 1.0f), Vec2(r.z + 1.0f, r.w + 1.0f),
                                   GetColorU32(Col_FocusRing), 2.0f);
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
    // frame
    float track_y = (frame.y + frame.w) * 0.5f;
    w->draw.AddRectFilledRounded(Vec2(frame.x, track_y - 3), Vec2(frame.z, track_y + 3),
                                 GetColorU32(held ? Col_FrameBgActive : Col_FrameBg), 3.0f);
    // grab
    float grab = s.grab_min_size;
    float t = (v_max > v_min) ? Clamp((*v - v_min) / (v_max - v_min), 0.0f, 1.0f) : 0.0f;
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
    if (changed) MarkItemEdited(id);
    return changed;
}
bool SliderFloat(const char* label, float* v, float v_min, float v_max, const char* fmt) {
    if (!label || !v || !FiniteFloat(*v) || !FiniteFloat(v_min) || !FiniteFloat(v_max)) return false;
    if (v_max < v_min) { float swap = v_min; v_min = v_max; v_max = swap; }
    return SliderScalar(label, v, v_min, v_max, fmt, false);
}
bool SliderInt(const char* label, int* v, int v_min, int v_max) {
    if(!GOG||!GOG->cur_window||!label||!v)return false;if(v_max<v_min){int swap=v_min;v_min=v_max;v_max=swap;}
    Window* w=GOG->cur_window;Style& style=GOG->style;ID id=GetID(label);const char* end=FindDisplayEnd(label);
    float width=CalcItemWidth(),height=GOG->atlas.line_height+style.frame_padding.y*2;Vec2 pos=w->cursor;Vec4 frame(pos.x,pos.y,pos.x+width,pos.y+height);
    Vec2 label_size=CalcTextSize(label,end);ItemSize(Vec2(width+style.item_inner_spacing.x+label_size.x,height));
    bool was_active=GOG->active_id==id;
    bool clicked_here=GOG->mouse_clicked[0]&&GOG->disabled_depth==0&&(!GOG->active_id||was_active)&&ItemContainsPoint(frame,GOG->mouse_clicked_pos[0],w);
    bool hovered=false,held=false;ButtonBehavior(frame,id,&hovered,&held);
    int old=*v;long long range=(long long)v_max-(long long)v_min;
    const auto SetFromPoint=[&](const Vec2& point){if(range<=0)return;float usable=Max(1.0f,width-style.grab_min_size);double t=Clamp((point.x-frame.x-style.grab_min_size*.5f)/usable,0.0f,1.0f);long long mapped=(long long)v_min+(long long)floor(t*(double)range+0.5);*v=(int)mapped;};
    if(clicked_here)SetFromPoint(GOG->mouse_clicked_pos[0]);
    bool integer_slider_enabled=GOG->disabled_depth==0&&(GOG->last_item.status_flags&ItemStatus_Visible)&&WindowInModalTree(*GOG,w);
    if(integer_slider_enabled&&GOG->mouse_released[0]&&(was_active||clicked_here))SetFromPoint(GOG->mouse_released_pos[0]);
    else if(held&&GOG->disabled_depth==0)SetFromPoint(GOG->io.mouse_pos);
    if(GOG->nav_id==id&&GOG->disabled_depth==0&&(GOG->last_item.status_flags&ItemStatus_Visible)&&WindowInModalTree(*GOG,w)){
        long long candidate=*v;if(GOG->key_pressed[Key_LeftArrow]||GOG->key_pressed[Key_DownArrow])--candidate;if(GOG->key_pressed[Key_RightArrow]||GOG->key_pressed[Key_UpArrow])++candidate;
        if(candidate<v_min)candidate=v_min;if(candidate>v_max)candidate=v_max;*v=(int)candidate;
    }
    double normalized=range>0?((double)std::max(v_min,std::min(v_max,*v))-(double)v_min)/(double)range:0.0;float track_y=(frame.y+frame.w)*.5f;float grab=style.grab_min_size;float gx=frame.x+(float)normalized*Max(0.0f,width-grab);
    w->draw.AddRectFilledRounded(Vec2(frame.x,track_y-3),Vec2(frame.z,track_y+3),GetColorU32(held?Col_FrameBgActive:Col_FrameBg),3);
    w->draw.AddRectFilledRounded(Vec2(frame.x,track_y-3),Vec2(gx+grab*.5f,track_y+3),GetColorU32(held?Col_SliderGrabActive:Col_SliderGrab),3);
    w->draw.AddCircleFilled(Vec2(gx+grab*.5f,track_y),7,GetColorU32(held?Col_SliderGrabActive:Col_SliderGrab),18);char text[64];snprintf(text,sizeof(text),"%d",*v);RenderTextClipped(frame,text,0,true);
    w->draw.AddText(Vec2(frame.z+style.item_inner_spacing.x,pos.y+style.frame_padding.y),GetColorU32(Col_Text),label,end);bool changed=*v!=old;if(changed){MarkItemEdited(id);GOG->events.push_back(Event(Event_ValueChanged,id,label));}return changed;
}

bool DragFloat(const char* label, float* value, float speed, float min_value, float max_value, const char* format) {
    if (!GOG || !GOG->cur_window || !label || !value) return false;
    Window* w=GOG->cur_window; Style& style=GOG->style; ID id=GetID(label); const char* end=FindDisplayEnd(label);
    float width=CalcItemWidth(), height=GOG->atlas.line_height+style.frame_padding.y*2; Vec2 pos=w->cursor;
    Vec4 rect(pos.x,pos.y,pos.x+width,pos.y+height); Vec2 label_size=CalcTextSize(label,end);
    ItemSize(Vec2(width+style.item_inner_spacing.x+label_size.x,height)); bool hovered=false,held=false;
    ButtonBehavior(rect,id,&hovered,&held); float old=*value;
    if(held && GOG->disabled_depth==0) *value += (GOG->io.mouse_pos.x-GOG->mouse_pos_prev.x)*speed;
    if(GOG->nav_id==id&&GOG->disabled_depth==0&&(GOG->last_item.status_flags&ItemStatus_Visible)&&WindowInModalTree(*GOG,w)){
        if(GOG->key_pressed[Key_LeftArrow]||GOG->key_pressed[Key_DownArrow])*value-=speed;
        if(GOG->key_pressed[Key_RightArrow]||GOG->key_pressed[Key_UpArrow])*value+=speed;
    }
    if(max_value>min_value)*value=Clamp(*value,min_value,max_value); bool changed=*value!=old;
    U32 bg=GetColorU32(held?Col_FrameBgActive:(hovered?Col_FrameBgHovered:Col_FrameBg));
    w->draw.AddRectFilledRounded(pos,Vec2(rect.z,rect.w),bg,style.frame_rounding);
    char text[96];snprintf(text,sizeof(text),format?format:"%.3f",*value);RenderTextClipped(rect,text,0,true);
    w->draw.AddText(Vec2(rect.z+style.item_inner_spacing.x,pos.y+style.frame_padding.y),GetColorU32(Col_Text),label,end);
    if(changed){MarkItemEdited(id);GOG->events.push_back(Event(Event_ValueChanged,id,label));}return changed;
}
bool DragInt(const char* label, int* value, float speed, int min_value, int max_value) {
    if(!GOG||!GOG->cur_window||!label||!value)return false;Window* w=GOG->cur_window;Style& style=GOG->style;ID id=GetID(label);const char* end=FindDisplayEnd(label);
    float width=CalcItemWidth(),height=GOG->atlas.line_height+style.frame_padding.y*2;Vec2 pos=w->cursor;Vec4 rect(pos.x,pos.y,pos.x+width,pos.y+height);Vec2 label_size=CalcTextSize(label,end);
    ItemSize(Vec2(width+style.item_inner_spacing.x+label_size.x,height));bool hovered=false,held=false;ButtonBehavior(rect,id,&hovered,&held);int old=*value;
    if(GOG->mouse_clicked[0]&&GOG->active_id==id)GOG->drag_accumulator[id]=0.0;
    if(held&&GOG->disabled_depth==0&&FiniteFloat(speed)){double& accumulator=GOG->drag_accumulator[id];accumulator+=(GOG->io.mouse_pos.x-GOG->mouse_pos_prev.x)*(double)speed;long long whole=(long long)accumulator;
        if(whole){long long candidate=(long long)*value+whole;if(candidate<(long long)std::numeric_limits<int>::min())candidate=std::numeric_limits<int>::min();if(candidate>(long long)std::numeric_limits<int>::max())candidate=std::numeric_limits<int>::max();*value=(int)candidate;accumulator-=whole;}}
    if(GOG->nav_id==id&&GOG->disabled_depth==0&&(GOG->last_item.status_flags&ItemStatus_Visible)&&WindowInModalTree(*GOG,w)){long long candidate=*value;
        if(GOG->key_pressed[Key_LeftArrow]||GOG->key_pressed[Key_DownArrow])--candidate;if(GOG->key_pressed[Key_RightArrow]||GOG->key_pressed[Key_UpArrow])++candidate;
        if(candidate<std::numeric_limits<int>::min())candidate=std::numeric_limits<int>::min();if(candidate>std::numeric_limits<int>::max())candidate=std::numeric_limits<int>::max();*value=(int)candidate;}
    if(max_value>min_value){if(*value<min_value)*value=min_value;if(*value>max_value)*value=max_value;}bool changed=*value!=old;U32 bg=GetColorU32(held?Col_FrameBgActive:(hovered?Col_FrameBgHovered:Col_FrameBg));
    w->draw.AddRectFilledRounded(pos,Vec2(rect.z,rect.w),bg,style.frame_rounding);char text[64];snprintf(text,sizeof(text),"%d",*value);RenderTextClipped(rect,text,0,true);w->draw.AddText(Vec2(rect.z+style.item_inner_spacing.x,pos.y+style.frame_padding.y),GetColorU32(Col_Text),label,end);
    if(changed){MarkItemEdited(id);GOG->events.push_back(Event(Event_ValueChanged,id,label));}return changed;
}
bool KnobFloat(const char* label,float* value,float min_value,float max_value,float speed,const Vec2& size_arg){
    if(!GOG||!GOG->cur_window||!label||!value||max_value<=min_value)return false;Window* w=GOG->cur_window;ID id=GetID(label);
    float width=Max(size_arg.x,48.0f),height=Max(size_arg.y,64.0f);Vec2 pos=w->cursor;Vec4 rect(pos.x,pos.y,pos.x+width,pos.y+height);ItemSize(Vec2(width,height));
    bool hovered=false,held=false;ButtonBehavior(rect,id,&hovered,&held);float old=*value;if(held&&GOG->disabled_depth==0){float delta=(GOG->io.mouse_pos.x-GOG->mouse_pos_prev.x)-(GOG->io.mouse_pos.y-GOG->mouse_pos_prev.y);*value+=delta*speed*(max_value-min_value);}
    *value=Clamp(*value,min_value,max_value);float t=(*value-min_value)/(max_value-min_value);Vec2 center(pos.x+width*.5f,pos.y+width*.43f);float radius=Min(width*.32f,24.0f);
    w->draw.AddCircleFilled(center,radius,GetColorU32(hovered?Col_FrameBgHovered:Col_FrameBg),28);w->draw.AddCircle(center,radius,GetColorU32(Col_Border),28,1.0f);
    float start=-2.3561945f,end_angle=start+t*4.712389f;for(int i=0;i<24;++i){float a0=start+(end_angle-start)*(float)i/24.0f,a1=start+(end_angle-start)*(float)(i+1)/24.0f;w->draw.AddLine(Vec2(center.x+cosf(a0)*(radius+3),center.y+sinf(a0)*(radius+3)),Vec2(center.x+cosf(a1)*(radius+3),center.y+sinf(a1)*(radius+3)),GetColorU32(Col_SliderGrabActive),2.5f);}
    w->draw.AddLine(center,Vec2(center.x+cosf(end_angle)*radius*.72f,center.y+sinf(end_angle)*radius*.72f),GetColorU32(Col_Text),2.0f);Vec2 ls=CalcTextSize(label,FindDisplayEnd(label));w->draw.AddText(Vec2(pos.x+(width-ls.x)*.5f,pos.y+height-GOG->atlas.line_height),GetColorU32(Col_Text),label,FindDisplayEnd(label));
    bool changed=*value!=old;if(changed){MarkItemEdited(id);GOG->events.push_back(Event(Event_ValueChanged,id,label));}return changed;
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
static bool DeleteSelection(char* buffer, int& length, int& cursor, int& anchor) {
    int first = std::min(cursor, anchor), last = std::max(cursor, anchor);
    if (first == last) return false;
    memmove(buffer + first, buffer + last, (size_t)(length - last + 1));
    length -= last - first;
    cursor = anchor = first;
    return true;
}
static bool AssignTextBuffer(char* buffer, int capacity, const std::string& value, int& length, int& cursor, int& anchor) {
    if (!buffer || capacity <= 0 || value.size() >= (size_t)capacity) return false;
    memcpy(buffer, value.c_str(), value.size() + 1);
    length = (int)value.size();
    cursor = anchor = length;
    return true;
}
static void PushTextHistory(std::map<ID, std::vector<std::string> >& histories, ID id, const std::string& state) {
    std::vector<std::string>& history = histories[id];
    if (!history.empty() && history.back() == state) return;
    history.push_back(state);
    if (history.size() > 64) history.erase(history.begin());
}
static int TextCharacterClass(const char* buffer, int length, int cursor) {
    if (cursor < 0 || cursor >= length) return 0;
    unsigned char c = (unsigned char)buffer[cursor];
    if (c >= 0x80 || (c >= '0' && c <= '9') || (c >= 'A' && c <= 'Z') ||
        (c >= 'a' && c <= 'z') || c == '_') return 1;
    if (c <= 32) return 0;
    return 2;
}
static int WordBoundaryLeft(const char* buffer, int length, int cursor) {
    int p = cursor;
    while (p > 0) {
        int previous = UTF8Prev(buffer, p);
        if (TextCharacterClass(buffer, length, previous) != 0) break;
        p = previous;
    }
    if (p <= 0) return 0;
    int previous = UTF8Prev(buffer, p);
    int character_class = TextCharacterClass(buffer, length, previous);
    while (p > 0) {
        previous = UTF8Prev(buffer, p);
        if (TextCharacterClass(buffer, length, previous) != character_class) break;
        p = previous;
    }
    return p;
}
static int WordBoundaryRight(const char* buffer, int length, int cursor) {
    int p = cursor;
    if (p < length) {
        int character_class = TextCharacterClass(buffer, length, p);
        while (p < length && TextCharacterClass(buffer, length, p) == character_class)
            p = UTF8Next(buffer, length, p);
    }
    while (p < length && TextCharacterClass(buffer, length, p) == 0)
        p = UTF8Next(buffer, length, p);
    return p;
}
static int LineBoundaryLeft(const char* buffer, int cursor) {
    while (cursor > 0 && buffer[cursor - 1] != '\n') --cursor;
    return cursor;
}
static int LineBoundaryRight(const char* buffer, int length, int cursor) {
    while (cursor < length && buffer[cursor] != '\n') ++cursor;
    return cursor;
}
static float InputTextSpanWidth(const char* buffer, int first, int last, bool password) {
    if (last <= first) return 0.0f;
    if (!password) return CalcTextSize(buffer + first, buffer + last).x;
    int count = 0;
    for (int cursor = first; cursor < last; cursor = UTF8Next(buffer, last, cursor))
        if (buffer[cursor] != '\n') ++count;
    return CalcTextSize("*").x * count;
}
static void RenderInputTextSelection(DrawList& draw, const char* buffer, int length, int first, int last,
                                     const Vec2& text_pos, bool password) {
    if (first >= last) return;
    const float line_height = GOG->atlas.line_height;
    const float newline_width = Max(3.0f, CalcTextSize(" ").x * 0.5f);
    int line_start = 0, line_number = 0;
    while (line_start <= length) {
        int line_end = line_start;
        while (line_end < length && buffer[line_end] != '\n') ++line_end;
        int selected_first = std::max(first, line_start);
        int selected_last = std::min(last, line_end);
        bool selects_newline = line_end < length && first <= line_end && last > line_end;
        if (selected_first < selected_last || selects_newline) {
            float x0 = text_pos.x + InputTextSpanWidth(buffer, line_start, selected_first, password);
            float x1 = text_pos.x + InputTextSpanWidth(buffer, line_start, selected_last, password);
            if (selects_newline) x1 += newline_width;
            draw.AddRectFilled(Vec2(x0, text_pos.y + line_number * line_height),
                               Vec2(Max(x0 + 1.0f, x1), text_pos.y + (line_number + 1) * line_height),
                               GetColorU32(Col_Selection));
        }
        if (line_end >= length) break;
        line_start = line_end + 1;
        ++line_number;
    }
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
static int BoundedTextLength(char* buffer, int capacity) {
    int length = 0;
    while (length < capacity && buffer[length] != 0) ++length;
    if (length < capacity) return length;
    buffer[capacity - 1] = 0;
    if (GOG) {
        GOG->last_error = "InputText buffer was not null-terminated within buffer_size; it was safely truncated";
        if (GOG->debug_log_callback)
            GOG->debug_log_callback(GOG->last_error.c_str(), GOG->debug_log_user_data);
    }
    return capacity - 1;
}
static bool InputTextEx(const char* label, char* buffer, int buffer_size, const Vec2& size_arg,
                        int flags, bool multiline) {
    if (!GOG || !GOG->cur_window || !label || !buffer || buffer_size <= 0) return false;
    Window* w = GOG->cur_window;
    Context& g = *GOG;
    Style& s = g.style;
    ID id = GetID(label);
    const char* label_end = FindDisplayEnd(label);
    Vec2 label_size = CalcTextSize(label, label_end);
    float width = size_arg.x > 0.0f ? size_arg.x : (size_arg.x < 0.0f ? Max(80.0f, w->content_w - (label_size.x > 0 ? label_size.x + s.item_inner_spacing.x : 0.0f)) : CalcItemWidth());
    float height = size_arg.y > 0.0f ? size_arg.y : g.atlas.line_height + s.frame_padding.y * 2.0f;
    Vec2 pos = w->cursor;
    Vec4 frame(pos.x, pos.y, pos.x + width, pos.y + height);
    Vec2 total(width + (label_size.x > 0 ? s.item_inner_spacing.x + label_size.x : 0.0f), height);
    ItemSize(total);
    RegisterFocusable(id);
    if (g.submitted_ids.find(id) != g.submitted_ids.end()) ++g.metrics.id_conflicts;
    else g.submitted_ids[id] = frame;
    g.last_item.id = id; g.last_item.rect = frame; g.last_item.status_flags &= ItemStatus_Visible;
    if (g.disabled_depth > 0) g.last_item.status_flags |= ItemStatus_Disabled;
    bool hovered = ItemHoverable(frame, id);
    Vec2 inner_pos(pos.x + s.frame_padding.x, pos.y + s.frame_padding.y);
    Vec2& scroll = g.text_scroll[id];
    scroll.x = Max(0.0f, scroll.x); scroll.y = Max(0.0f, scroll.y);
    Vec2 text_pos(inner_pos.x - scroll.x, inner_pos.y - scroll.y);
    int length = BoundedTextLength(buffer, buffer_size);
    int& cursor = g.text_cursor[id];
    int& anchor = g.text_selection_anchor[id];
    if (cursor < 0 || cursor > length) cursor = length;
    if (anchor < 0 || anchor > length) anchor = cursor;
    const bool password = (flags & InputTextFlags_Password) != 0;
    const bool was_active = g.text_active_id == id;
    const bool clicked_here = g.mouse_clicked[0] && g.disabled_depth == 0 &&
                              ItemContainsPoint(frame, g.mouse_clicked_pos[0], w);
    const bool just_activated_by_mouse = clicked_here && !was_active;
    if (clicked_here) {
        g.text_active_id = id; g.text_active_id_window = w; g.nav_id = id; FocusWindow(w);
        int clicked_cursor = CursorFromPoint(buffer, length, g.mouse_clicked_pos[0], text_pos, multiline);
        bool click_shift = (g.mouse_clicked_mods[0] & InputMod_Shift) != 0;
        if (!was_active || !click_shift) anchor = clicked_cursor;
        cursor = clicked_cursor;
        if (!was_active && (flags & InputTextFlags_AutoSelectAll)) { anchor = 0; cursor = length; }
        g.active_id = id; g.active_id_window = w;
    }
    bool just_activated_by_nav = false;
    Key nav_activation_key = Key_None;
    if (g.disabled_depth == 0 && WindowInModalTree(g, w) &&
        (g.last_item.status_flags & ItemStatus_Visible) && g.nav_activate_id == id && !was_active) {
        nav_activation_key = g.nav_activate_key;
        ConsumeNavActivate(id);
        g.text_active_id = id; g.text_active_id_window = w; cursor = length;
        anchor = (flags & InputTextFlags_AutoSelectAll) ? 0 : cursor;
        just_activated_by_nav = true;
    }
    bool active = g.disabled_depth == 0 && g.text_active_id == id;
    if (active && g.active_id == id && g.io.mouse_down[0] && !g.mouse_clicked[0])
        cursor = CursorFromPoint(buffer, length, g.io.mouse_pos, text_pos, multiline);
    if (g.active_id == id && g.mouse_released[0]) { g.active_id = 0; g.active_id_window = 0; }
    bool changed = false, submitted = false;
    bool read_only = (flags & InputTextFlags_ReadOnly) != 0;
    bool edit_snapshot_taken = false;
    std::string edit_before;
    const bool undo_enabled = (flags & InputTextFlags_NoUndoRedo) == 0;
    const auto SnapshotBeforeEdit = [&]() {
        if (!edit_snapshot_taken) { edit_before.assign(buffer, (size_t)length); edit_snapshot_taken = true; }
    };
    const auto CommitEditSnapshot = [&]() {
        if (!edit_snapshot_taken) return;
        std::string current(buffer, (size_t)length);
        if (undo_enabled && current != edit_before) {
            PushTextHistory(g.text_undo, id, edit_before);
            g.text_redo.erase(id);
        }
        edit_snapshot_taken = false;
        edit_before.clear();
    };
    if (active) {
        const auto MoveCursor = [&](int destination, bool keep_selection) {
            cursor = destination < 0 ? 0 : (destination > length ? length : destination);
            if (!keep_selection) anchor = cursor;
        };
        const auto PerformUndo = [&]() {
            CommitEditSnapshot();
            if (!read_only && undo_enabled) {
                std::vector<std::string>& undo = g.text_undo[id];
                if (!undo.empty()) {
                    std::string current(buffer, (size_t)length);
                    std::string target = undo.back();
                    if (AssignTextBuffer(buffer, buffer_size, target, length, cursor, anchor)) {
                        undo.pop_back(); PushTextHistory(g.text_redo, id, current); changed = true;
                    }
                }
            }
        };
        const auto PerformRedo = [&]() {
            CommitEditSnapshot();
            if (!read_only && undo_enabled) {
                std::vector<std::string>& redo = g.text_redo[id];
                if (!redo.empty()) {
                    std::string current(buffer, (size_t)length);
                    std::string target = redo.back();
                    if (AssignTextBuffer(buffer, buffer_size, target, length, cursor, anchor)) {
                        redo.pop_back(); PushTextHistory(g.text_undo, id, current); changed = true;
                    }
                }
            }
        };
        const auto InsertCodepoint = [&](unsigned int codepoint) {
            if (read_only || codepoint < 32 || codepoint == 127 || codepoint > 0x10FFFF ||
                (codepoint >= 0xD800 && codepoint <= 0xDFFF)) return;
            char encoded[5]; int count = EncodeUTF8(codepoint, encoded);
            SnapshotBeforeEdit();
            changed |= DeleteSelection(buffer, length, cursor, anchor);
            if (InsertBytes(buffer, buffer_size, length, cursor, encoded, count)) changed = true;
            anchor = cursor;
        };
        const auto ConsumeKey = [&](Key key) {
            int key_index = (int)key;
            if (key_index > 0 && key_index < Key_COUNT) g.key_consumed[key_index] = true;
        };
        const auto HandleKey = [&](Key key, int modifiers) {
            const bool shift = (modifiers & InputMod_Shift) != 0;
            const bool ctrl = (modifiers & InputMod_Ctrl) != 0;
            const bool shortcut = ctrl && (modifiers & InputMod_Alt) == 0;
            if (key == Key_Escape) {
                ConsumeKey(key);
                if (!read_only && (flags & InputTextFlags_EscapeClearsAll) && length > 0) {
                    SnapshotBeforeEdit(); buffer[0] = 0; length = cursor = anchor = 0; changed = true;
                } else { g.text_active_id = 0; g.text_active_id_window = 0; }
                return;
            }
            if (shortcut && key == Key_Z) {
                ConsumeKey(key);
                if (shift) PerformRedo(); else PerformUndo();
                return;
            }
            if (shortcut && key == Key_Y) { ConsumeKey(key); PerformRedo(); return; }
            if (shortcut && key == Key_A) { ConsumeKey(key); anchor = 0; cursor = length; return; }
            if (key == Key_LeftArrow) {
                ConsumeKey(key);
                if (!shift && cursor != anchor) MoveCursor(std::min(cursor, anchor), false);
                else MoveCursor(ctrl ? WordBoundaryLeft(buffer, length, cursor) : UTF8Prev(buffer, cursor), shift);
                return;
            }
            if (key == Key_RightArrow) {
                ConsumeKey(key);
                if (!shift && cursor != anchor) MoveCursor(std::max(cursor, anchor), false);
                else MoveCursor(ctrl ? WordBoundaryRight(buffer, length, cursor) : UTF8Next(buffer, length, cursor), shift);
                return;
            }
            if (key == Key_Home) { ConsumeKey(key); MoveCursor((ctrl || !multiline) ? 0 : LineBoundaryLeft(buffer, cursor), shift); return; }
            if (key == Key_End) { ConsumeKey(key); MoveCursor((ctrl || !multiline) ? length : LineBoundaryRight(buffer, length, cursor), shift); return; }
            if (multiline && (key == Key_UpArrow || key == Key_DownArrow || key == Key_PageUp || key == Key_PageDown)) {
                ConsumeKey(key);
                Vec2 caret; CursorScreenPosition(buffer, cursor, text_pos, password, caret);
                int direction = (key == Key_UpArrow || key == Key_PageUp) ? -1 : 1;
                int lines = (key == Key_PageUp || key == Key_PageDown)
                            ? std::max(1, (int)(height / Max(1.0f, g.atlas.line_height)) - 1) : 1;
                Vec2 target(caret.x, caret.y + direction * lines * g.atlas.line_height + g.atlas.line_height * 0.5f);
                MoveCursor(CursorFromPoint(buffer, length, target, text_pos, true), shift);
                return;
            }
            int selection_first = std::min(cursor, anchor), selection_last = std::max(cursor, anchor);
            if (shortcut && key == Key_C && selection_first < selection_last && !password && g.io.set_clipboard_text) {
                ConsumeKey(key);
                std::string selected(buffer + selection_first, buffer + selection_last);
                g.io.set_clipboard_text(g.io.clipboard_user_data, selected.c_str());
                return;
            }
            if (!read_only && shortcut && key == Key_X && selection_first < selection_last) {
                ConsumeKey(key);
                if (!password && g.io.set_clipboard_text) {
                    std::string selected(buffer + selection_first, buffer + selection_last);
                    g.io.set_clipboard_text(g.io.clipboard_user_data, selected.c_str());
                }
                SnapshotBeforeEdit(); changed |= DeleteSelection(buffer, length, cursor, anchor);
                return;
            }
            if (!read_only && shortcut && key == Key_V && g.io.get_clipboard_text) {
                ConsumeKey(key);
                const char* clip = g.io.get_clipboard_text(g.io.clipboard_user_data);
                if (clip && *clip) {
                    SnapshotBeforeEdit(); changed |= DeleteSelection(buffer, length, cursor, anchor);
                    for (const char* p = clip; *p; ) {
                        unsigned int cp = DecodeUTF8(p, 0);
                        if (cp == '\r' || (!multiline && cp == '\n')) continue;
                        char encoded[5]; int count = EncodeUTF8(cp, encoded);
                        if (InsertBytes(buffer, buffer_size, length, cursor, encoded, count)) changed = true;
                    }
                    anchor = cursor;
                }
                return;
            }
            if (!read_only && key == Key_Backspace && (cursor != anchor || cursor > 0)) {
                ConsumeKey(key);
                SnapshotBeforeEdit();
                if (cursor == anchor) anchor = ctrl ? WordBoundaryLeft(buffer, length, cursor) : UTF8Prev(buffer, cursor);
                changed |= DeleteSelection(buffer, length, cursor, anchor);
                return;
            }
            if (!read_only && key == Key_Delete && (cursor != anchor || cursor < length)) {
                ConsumeKey(key);
                SnapshotBeforeEdit();
                if (cursor == anchor) anchor = ctrl ? WordBoundaryRight(buffer, length, cursor) : UTF8Next(buffer, length, cursor);
                changed |= DeleteSelection(buffer, length, cursor, anchor);
                return;
            }
            if (key == Key_Enter) {
                ConsumeKey(key);
                bool insert_newline = multiline && (!(flags & InputTextFlags_CtrlEnterForNewLine) || ctrl);
                if (insert_newline && !read_only) {
                    SnapshotBeforeEdit(); changed |= DeleteSelection(buffer, length, cursor, anchor);
                    const char newline = '\n';
                    if (InsertBytes(buffer, buffer_size, length, cursor, &newline, 1)) changed = true;
                    anchor = cursor;
                } else {
                    submitted = true;
                    if (flags & InputTextFlags_EnterReturnsTrue) { g.text_active_id = 0; g.text_active_id_window = 0; }
                }
                return;
            }
        };

        bool event_key_state[Key_COUNT];
        bool queued_key_press[Key_COUNT];
        for (int key = 0; key < Key_COUNT; ++key) {
            event_key_state[key] = g.key_down_prev[key];
            queued_key_press[key] = false;
        }
        bool queued_text = false;
        bool activation_key_consumed = false;
        bool activation_character_consumed = false;
        bool queued_mouse_activation = false;
        bool queued_mouse_state = g.mouse_down_prev[0];
        for (size_t event_index = 0; event_index < g.io.input_events.size(); ++event_index) {
            const InputEvent& event = g.io.input_events[event_index];
            if (event.type == InputEvent_MouseButton && event.key_or_button == 0) {
                if (event.down && !queued_mouse_state &&
                    ItemContainsPoint(frame, Vec2(event.x, event.y), w)) queued_mouse_activation = true;
                queued_mouse_state = event.down;
            }
        }
        bool reached_activation = !just_activated_by_nav && !(just_activated_by_mouse && queued_mouse_activation);
        bool replay_mouse_state = g.mouse_down_prev[0];
        for (size_t event_index = 0; event_index < g.io.input_events.size(); ++event_index) {
            const InputEvent& event = g.io.input_events[event_index];
            if (event.type == InputEvent_Key && event.key_or_button > 0 && event.key_or_button < Key_COUNT) {
                int key_index = event.key_or_button;
                bool was_down = event_key_state[key_index];
                event_key_state[key_index] = event.down;
                if (event.down && !was_down) {
                    queued_key_press[key_index] = true;
                    Key key = (Key)key_index;
                    if (just_activated_by_nav && !activation_key_consumed && key == nav_activation_key) {
                        reached_activation = true;
                        activation_key_consumed = true;
                    } else if (reached_activation && g.text_active_id == id) {
                        HandleKey(key, ModifierMask(event_key_state));
                    }
                }
            } else if (event.type == InputEvent_Text) {
                queued_text = true;
                if (!reached_activation) {
                    continue;
                } else if (just_activated_by_nav && nav_activation_key == Key_Space && !activation_character_consumed &&
                    event.codepoint == (unsigned int)' ') {
                    activation_character_consumed = true;
                } else if (g.text_active_id == id) {
                    InsertCodepoint(event.codepoint);
                }
            } else if (event.type == InputEvent_MouseButton && event.key_or_button == 0) {
                bool was_mouse_down = replay_mouse_state;
                replay_mouse_state = event.down;
                if (!reached_activation && just_activated_by_mouse && event.down && !was_mouse_down &&
                    ItemContainsPoint(frame, Vec2(event.x, event.y), w))
                    reached_activation = true;
            } else if (event.type == InputEvent_Focus && !event.down) {
                for (int key = 0; key < Key_COUNT; ++key) event_key_state[key] = false;
            }
        }
        static const Key fallback_keys[] = {
            Key_Escape, Key_Z, Key_Y, Key_A, Key_C, Key_X, Key_V,
            Key_LeftArrow, Key_RightArrow, Key_Home, Key_End,
            Key_UpArrow, Key_DownArrow, Key_PageUp, Key_PageDown,
            Key_Backspace, Key_Delete, Key_Enter
        };
        for (size_t key_index = 0; key_index < sizeof(fallback_keys) / sizeof(fallback_keys[0]); ++key_index) {
            Key key = fallback_keys[key_index];
            bool repeat = key == Key_LeftArrow || key == Key_RightArrow || key == Key_Home || key == Key_End ||
                          key == Key_UpArrow || key == Key_DownArrow || key == Key_PageUp || key == Key_PageDown ||
                          key == Key_Backspace || key == Key_Delete;
            if (!queued_key_press[(int)key] && IsKeyPressed(key, repeat)) {
                if (just_activated_by_nav && !activation_key_consumed && key == nav_activation_key) {
                    reached_activation = true;
                    activation_key_consumed = true;
                } else if (reached_activation && g.text_active_id == id)
                    HandleKey(key, ModifierMask(g.io.key_down));
            }
        }
        if (!queued_text && reached_activation && g.text_active_id == id) {
            for (size_t char_index = 0; char_index < g.io.input_chars.size(); ++char_index) {
                unsigned int codepoint = g.io.input_chars[char_index];
                if (just_activated_by_nav && nav_activation_key == Key_Space && !activation_character_consumed &&
                    codepoint == (unsigned int)' ') {
                    activation_character_consumed = true;
                } else InsertCodepoint(codepoint);
            }
        }
        CommitEditSnapshot();
    }
    active = g.disabled_depth == 0 && g.text_active_id == id;
    if (active) {
        Vec2 caret; CursorScreenPosition(buffer, cursor, text_pos, password, caret);
        const float left = frame.x + s.frame_padding.x;
        const float right = frame.z - s.frame_padding.x - 1.0f;
        const float top = frame.y + s.frame_padding.y;
        const float bottom = frame.w - s.frame_padding.y - g.atlas.line_height;
        if (caret.x < left) scroll.x = Max(0.0f, scroll.x - (left - caret.x));
        else if (caret.x > right) scroll.x += caret.x - right;
        if (multiline) {
            if (caret.y < top) scroll.y = Max(0.0f, scroll.y - (top - caret.y));
            else if (caret.y > bottom) scroll.y += caret.y - bottom;
        } else scroll.y = 0.0f;
        text_pos = Vec2(inner_pos.x - scroll.x, inner_pos.y - scroll.y);
    }
    U32 bg = GetColorU32(active ? Col_FrameBgActive : (hovered ? Col_FrameBgHovered : Col_FrameBg));
    w->draw.AddRectFilledRounded(pos, Vec2(frame.z, frame.w), bg, s.frame_rounding);
    if (active || g.nav_id == id)
        w->draw.AddRect(pos, Vec2(frame.z, frame.w), ColorWithAlpha(GetColorU32(Col_FocusRing), active ? 230 : 120), 1.0f);
    w->draw.PushClipRect(Vec4(frame.x + 1, frame.y + 1, frame.z - 1, frame.w - 1));
    if (active && cursor != anchor)
        RenderInputTextSelection(w->draw, buffer, length, std::min(cursor, anchor), std::max(cursor, anchor), text_pos, password);
    if (password) {
        std::string masked;
        for (int i = 0; i < length; ) {
            int next = UTF8Next(buffer, length, i);
            masked += buffer[i] == '\n' ? '\n' : '*';
            i = next;
        }
        w->draw.AddText(text_pos, GetColorU32(Col_Text), masked.c_str());
    } else w->draw.AddText(text_pos, GetColorU32(Col_Text), buffer);
    if (active && fmod(g.time, 1.0) < 0.55) {
        Vec2 caret; CursorScreenPosition(buffer, cursor, text_pos, password, caret);
        w->draw.AddLine(caret, Vec2(caret.x, caret.y + g.atlas.line_height), GetColorU32(Col_Text), 1.0f);
    }
    w->draw.PopClipRect();
    if (label_size.x > 0)
        w->draw.AddText(Vec2(frame.z + s.item_inner_spacing.x, pos.y + s.frame_padding.y), GetColorU32(Col_Text), label, label_end);
    if (hovered) g.last_item.status_flags |= ItemStatus_Hovered;
    if (active) g.last_item.status_flags |= ItemStatus_Active;
    if (g.nav_id == id) g.last_item.status_flags |= ItemStatus_Focused;
    if (changed) { g.last_item.status_flags |= ItemStatus_Edited; g.events.push_back(Event(Event_TextChanged, id, label)); }
    return changed || (submitted && (flags & InputTextFlags_EnterReturnsTrue));
}
bool InputText(const char* label, char* buffer, int buffer_size, int flags) {
    return InputTextEx(label, buffer, buffer_size, Vec2(0, 0), flags, false);
}
bool InputTextMultiline(const char* label, char* buffer, int buffer_size, const Vec2& size, int flags) {
    return InputTextEx(label, buffer, buffer_size, size, flags, true);
}

bool Combo(const char* label, int* current_item, const char* const items[], int item_count) {
    if (!current_item || !items || item_count <= 0) return false;
    ID id = GetID(label);
    int& open = GOG->storage[id ^ 0xC04B0ULL];
    const char* preview = (*current_item >= 0 && *current_item < item_count) ? items[*current_item] : "Select...";
    std::string button_label(preview ? preview : "");
    button_label += "   v###combo_preview";
    bool changed = false;
    PushID(label);
    if (Button(button_label.c_str(), Vec2(CalcItemWidth(), 0))) open = !open;
    SameLine(); Text("%s", label);
    if (open) {
        Indent(8.0f);
        for (int i = 0; i < item_count; ++i) {
            std::string item_label = i == *current_item ? "* " : "  ";
            item_label += items[i] ? items[i] : "";
            item_label += "###combo_item";
            PushID(i);
            bool selected = Button(item_label.c_str(), Vec2(CalcItemWidth() - 8.0f, 0));
            PopID();
            if (selected) {
                *current_item = i; open = 0; changed = true;
                GOG->events.push_back(Event(Event_ValueChanged, id, label));
            }
        }
        Unindent(8.0f);
    }
    PopID();
    return changed;
}

struct TabBarState { ID id; ID selected; std::vector<std::string> labels; bool item_scope_open; TabBarState() : id(0), selected(0), item_scope_open(false) {} };
static thread_local std::map<Context*, std::vector<TabBarState> > GTabBarsByContext;
static thread_local std::map<Context*, std::map<ID, ID> > GTabSelectionsByContext;
static thread_local std::map<Context*, std::map<ID, std::vector<std::string> > > GTabLabelsByContext;
static std::vector<TabBarState>& TabBars(){return GTabBarsByContext[GOG];}
static std::map<ID,ID>& TabSelections(){return GTabSelectionsByContext[GOG];}
static std::map<ID,std::vector<std::string> >& TabLabels(){return GTabLabelsByContext[GOG];}
bool BeginTabBar(const char* id) {
    if(!GOG||!GOG->cur_window)return false;
    TabBarState state; state.id = GetID(id ? id : "tabs"); state.selected = TabSelections()[state.id];
    std::vector<std::string>& previous=TabLabels()[state.id];
    bool selection_exists=false;for(size_t i=0;i<previous.size();++i)if(HashLabelID(previous[i].c_str(),state.id)==state.selected)selection_exists=true;
    if((!state.selected||!selection_exists)&&!previous.empty()){state.selected=HashLabelID(previous[0].c_str(),state.id);TabSelections()[state.id]=state.selected;}
    for(size_t i=0;i<previous.size();++i){ID tab_id=HashLabelID(previous[i].c_str(),state.id);U32 old=GOG->style.colors[Col_Button];if(state.selected==tab_id)GOG->style.colors[Col_Button]=GOG->style.colors[Col_ButtonActive];GOG->cur_window->id_stack.push_back(tab_id);if(Button(previous[i].c_str()))state.selected=tab_id;GOG->cur_window->id_stack.pop_back();GOG->style.colors[Col_Button]=old;if(i+1<previous.size())SameLine();}
    if(!previous.empty())Separator();TabSelections()[state.id]=state.selected;
    GOG->cur_window->id_stack.push_back(state.id);TabBars().push_back(state); return true;
}
bool BeginTabItem(const char* label, bool* p_open) {
    if (TabBars().empty() || (p_open && !*p_open)) return false;
    TabBarState& bar = TabBars().back();
    if(bar.item_scope_open){GOG->last_error="BeginTabItem() called before the previous EndTabItem()";return false;}
    const char* item_label=label?label:"Tab";ID id = HashLabelID(item_label, bar.id);
    bar.labels.push_back(item_label);
    if (!bar.selected) { bar.selected = id; TabSelections()[bar.id] = id; }
    if(bar.selected!=id)return false;GOG->cur_window->id_stack.push_back(id);bar.item_scope_open=true;return true;
}
void EndTabItem() {
    if(TabBars().empty()||!TabBars().back().item_scope_open){if(GOG)GOG->last_error="EndTabItem() called without a matching selected tab";return;}
    if(GOG->cur_window&&GOG->cur_window->id_stack.size()>1)GOG->cur_window->id_stack.pop_back();TabBars().back().item_scope_open=false;
}
void EndTabBar() {
    if (TabBars().empty()) return;
    if(TabBars().back().item_scope_open)EndTabItem();TabBarState bar = TabBars().back(); TabBars().pop_back();
    if(GOG->cur_window&&GOG->cur_window->id_stack.size()>1)GOG->cur_window->id_stack.pop_back();
    bool selection_exists=false;for(size_t i=0;i<bar.labels.size();++i)if(HashLabelID(bar.labels[i].c_str(),bar.id)==bar.selected)selection_exists=true;
    if(!selection_exists)bar.selected=bar.labels.empty()?0:HashLabelID(bar.labels[0].c_str(),bar.id);
    TabSelections()[bar.id] = bar.selected;TabLabels()[bar.id]=bar.labels;
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
    if (open) { w->id_stack.push_back(id); GOG->tree_scope_stack.push_back(id); Indent(); }
    return open != 0;
}
void TreePop() {
    if(!GOG||!GOG->cur_window||GOG->tree_scope_stack.empty()){if(GOG)GOG->last_error="TreePop() called without a matching open TreeNode()";return;}
    Unindent();if(GOG->cur_window->id_stack.size()>1)GOG->cur_window->id_stack.pop_back();GOG->tree_scope_stack.pop_back();
}

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

void StatusBadge(const char* label, U32 color, bool pulse) {
    if(!GOG||!GOG->cur_window||!label)return;Window* w=GOG->cur_window;Style& s=GOG->style;if(!color)color=GetColorU32(Col_Success);
    Vec2 text=CalcTextSize(label),size(text.x+s.frame_padding.x*2+16.0f,text.y+s.frame_padding.y);Vec2 pos=w->cursor;ItemSize(size);
    w->draw.AddRectFilledRounded(pos,Vec2(pos.x+size.x,pos.y+size.y),ColorWithAlpha(color,42),size.y*.5f);
    float glow=pulse&&!IsReducedMotion()?(0.5f+0.5f*sinf((float)GOG->time*5.0f)):0.0f;
    if(glow>0)w->draw.AddCircleFilled(Vec2(pos.x+10,pos.y+size.y*.5f),6.0f+glow*2.0f,ColorWithAlpha(color,(int)(45*glow)),16);
    w->draw.AddCircleFilled(Vec2(pos.x+10,pos.y+size.y*.5f),3.5f,color,14);w->draw.AddText(Vec2(pos.x+18,pos.y+(size.y-text.y)*.5f),GetColorU32(Col_Text),label);
}
void Spinner(const char* label,float radius,float thickness,U32 color){
    if(!GOG||!GOG->cur_window||radius<=0||thickness<=0)return;Window* w=GOG->cur_window;if(!color)color=GetColorU32(Col_ButtonHovered);Vec2 pos=w->cursor;Vec2 label_size=CalcTextSize(label?label:"");float diameter=radius*2;
    ItemSize(Vec2(diameter+(label_size.x>0?GOG->style.item_inner_spacing.x+label_size.x:0),Max(diameter,label_size.y)));Vec2 center(pos.x+radius,pos.y+radius);float phase=IsReducedMotion()?0.0f:(float)GOG->time*5.2f;
    const int segments=22;for(int i=0;i<segments;++i){float t0=(float)i/segments,t1=(float)(i+1)/segments;float a0=phase+t0*4.8f,a1=phase+t1*4.8f;int alpha=(int)(40+215*t1);w->draw.AddLine(Vec2(center.x+cosf(a0)*radius,center.y+sinf(a0)*radius),Vec2(center.x+cosf(a1)*radius,center.y+sinf(a1)*radius),ColorWithAlpha(color,alpha),thickness);}
    if(label_size.x>0)w->draw.AddText(Vec2(pos.x+diameter+GOG->style.item_inner_spacing.x,pos.y+(diameter-label_size.y)*.5f),GetColorU32(Col_Text),label);
}
void Skeleton(const Vec2& size_arg,float rounding){
    if(!GOG||!GOG->cur_window)return;Window* w=GOG->cur_window;float width=size_arg.x>0?size_arg.x:Max(1.0f,GetContentRegionAvail().x+size_arg.x);float height=Max(size_arg.y,8.0f);Vec2 pos=w->cursor;ItemSize(Vec2(width,height));if(rounding<0)rounding=GOG->style.frame_rounding;
    U32 base=ColorWithAlpha(GetColorU32(Col_FrameBg),220);w->draw.AddRectFilledRounded(pos,Vec2(pos.x+width,pos.y+height),base,rounding);
    float phase=IsReducedMotion()?0.45f:fmodf((float)GOG->time*.65f,1.0f);float band=width*.32f;float x=pos.x-band+phase*(width+band*2);w->draw.PushClipRect(Vec4(pos.x,pos.y,pos.x+width,pos.y+height));w->draw.AddRectFilledMultiColor(Vec2(x-band,pos.y),Vec2(x+band,pos.y+height),ColorWithAlpha(OG_COL32_WHITE,0),ColorWithAlpha(OG_COL32_WHITE,38),ColorWithAlpha(OG_COL32_WHITE,38),ColorWithAlpha(OG_COL32_WHITE,0));w->draw.PopClipRect();
}
void MetricCard(const char* label,const char* value,const char* detail,U32 accent,const Vec2& size_arg){
    if(!GOG||!GOG->cur_window||!label||!value)return;Window* w=GOG->cur_window;Style& s=GOG->style;if(!accent)accent=GetColorU32(Col_Button);
    float width=size_arg.x>0?size_arg.x:Max(1.0f,GetContentRegionAvail().x+size_arg.x),height=size_arg.y>0?size_arg.y:82.0f;Vec2 pos=w->cursor;ItemSize(Vec2(width,height));w->draw.AddShadowRect(pos,Vec2(pos.x+width,pos.y+height),GetColorU32(Col_WindowShadow),s.frame_rounding+3,6);w->draw.AddRectFilledRounded(pos,Vec2(pos.x+width,pos.y+height),GetColorU32(Col_ChildBg),s.frame_rounding+3);w->draw.AddRectFilledRounded(pos,Vec2(pos.x+4,pos.y+height),accent,2);w->draw.AddText(Vec2(pos.x+16,pos.y+10),GetColorU32(Col_TextDisabled),label);w->draw.AddText(Vec2(pos.x+16,pos.y+31),GetColorU32(Col_Text),value);if(detail)w->draw.AddText(Vec2(pos.x+16,pos.y+height-GOG->atlas.line_height-8),ColorWithAlpha(accent,230),detail);w->draw.AddRect(pos,Vec2(pos.x+width,pos.y+height),ColorWithAlpha(GetColorU32(Col_Border),120),1);
}

void Image(TextureID texture_id, const Vec2& size, const Vec2& uv0, const Vec2& uv1, U32 tint) {
    if (!GOG->cur_window || size.x <= 0 || size.y <= 0) return;
    Window* w = GOG->cur_window; Vec2 pos = w->cursor; ItemSize(size);
    w->draw.PushTexture(texture_id);
    DrawIdx base = (DrawIdx)w->draw.vtx.size(); w->draw.PrimReserve(6, 4);
    DrawVert v; v.col = tint;
    v.pos = pos; v.uv = uv0; w->draw.vtx.push_back(v);
    v.pos = Vec2(pos.x + size.x, pos.y); v.uv = Vec2(uv1.x, uv0.y); w->draw.vtx.push_back(v);
    v.pos = Vec2(pos.x + size.x, pos.y + size.y); v.uv = uv1; w->draw.vtx.push_back(v);
    v.pos = Vec2(pos.x, pos.y + size.y); v.uv = Vec2(uv0.x, uv1.y); w->draw.vtx.push_back(v);
    w->draw.idx.push_back(base); w->draw.idx.push_back(base + 1); w->draw.idx.push_back(base + 2);
    w->draw.idx.push_back(base); w->draw.idx.push_back(base + 2); w->draw.idx.push_back(base + 3);
    w->draw.PopTexture();
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
static thread_local std::map<Context*, std::vector<ToastData> > GToastsByContext;
static std::vector<ToastData>& Toasts(){return GToastsByContext[GOG];}
void AddToast(const char* message, ToastType type, float duration) {
    if (!GOG || !message || !*message) return;
    ToastData toast; toast.message = message; toast.type = type; toast.expires = GOG->time + Max(duration, 0.25f); Toasts().push_back(toast);
    if(Toasts().size()>32)Toasts().erase(Toasts().begin());
}
void RenderNotifications() {
    DrawList& dl = GOG->overlay_draw;
    float y = 18.0f;
    for (size_t i = 0; i < Toasts().size();) {
        if (Toasts()[i].expires <= GOG->time) { Toasts().erase(Toasts().begin() + (ptrdiff_t)i); continue; }
        ToastData& toast = Toasts()[i]; Vec2 ts = CalcTextSize(toast.message.c_str());
        float width = Max(ts.x + 48.0f, 220.0f); float x = GOG->io.display_size.x - width - 18.0f;
        U32 accent = toast.type == Toast_Success ? GetColorU32(Col_Success) : toast.type == Toast_Warning ? GetColorU32(Col_Warning) : toast.type == Toast_Error ? OG_COL32(238,82,106,255) : GetColorU32(Col_Link);
        dl.AddShadowRect(Vec2(x,y), Vec2(x+width,y+46), GetColorU32(Col_WindowShadow), 9, 8);
        dl.AddBackdropBlur(Vec2(x,y), Vec2(x+width,y+46), ColorWithAlpha(GetColorU32(Col_TitleBgActive), 225), 6, 9);
        dl.AddRectFilledRounded(Vec2(x,y), Vec2(x+4,y+46), accent, 2);
        dl.AddText(Vec2(x+18,y+14), GetColorU32(Col_Text), toast.message.c_str());
        y += 56.0f; ++i;
    }
}

static thread_local std::map<Context*, std::map<ID, bool> > GPopupOpenByContext;
static thread_local std::map<Context*, std::vector<Window*> > GPopupParentsByContext;
static thread_local std::map<Context*, std::vector<ID> > GPopupIDsByContext;
static std::map<ID, bool>& PopupOpen() { return GPopupOpenByContext[GOG]; }
static std::vector<Window*>& PopupParents() { return GPopupParentsByContext[GOG]; }
static std::vector<ID>& PopupIDs() { return GPopupIDsByContext[GOG]; }
static Window* NearestOpenModalParent(Window* immediate_parent) {
    if (immediate_parent && (immediate_parent->flags & WindowFlags_InternalModal)) return immediate_parent;
    std::vector<Window*>& parents = PopupParents();
    for (std::vector<Window*>::reverse_iterator it = parents.rbegin(); it != parents.rend(); ++it)
        if (*it && ((*it)->flags & WindowFlags_InternalModal)) return *it;
    return 0;
}
void OpenPopup(const char* id) { if (GOG && GOG->cur_window) PopupOpen()[GetID(id)] = true; }
static bool BeginPopupEx(const char* id, bool* p_open, bool modal) {
    if (!GOG || !GOG->cur_window || !id || !*id) return false;
    ID popup_id = GetID(id);
    std::ostringstream popup_name;
    popup_name << id << "###popup_" << (unsigned long long)popup_id;
    std::string name = popup_name.str();
    Window* existing = FindWindow(name.c_str());
    if (!PopupOpen()[popup_id] || (p_open && !*p_open)) {
        PopupOpen()[popup_id] = false;
        if (modal && GOG->modal_window == existing) GOG->modal_window = NearestOpenModalParent(GOG->cur_window);
        return false;
    }
    Window* parent = GOG->cur_window;
    if (!modal && existing && existing->active_last_frame && GOG->mouse_clicked[0] &&
        !WindowDescendsFrom(WindowAtPoint(GOG->mouse_clicked_pos[0]), existing)) {
        PopupOpen()[popup_id] = false; return false;
    }
    Vec2 popup_size = modal ? Vec2(360, 220) : Vec2(300, 190);
    Vec2 at = modal ? Vec2(Max(8.0f, (GOG->io.display_size.x - popup_size.x) * 0.5f),
                           Max(8.0f, (GOG->io.display_size.y - popup_size.y) * 0.5f))
                    : Vec2(GOG->io.mouse_pos.x + 8, GOG->io.mouse_pos.y + 8);
    SetNextWindowPos(at, Cond_Appearing); SetNextWindowSize(popup_size, Cond_FirstUseEver);
    PopupParents().push_back(parent); PopupIDs().push_back(popup_id);
    int popup_flags = WindowFlags_NoCollapse | WindowFlags_NoSavedSettings | (modal ? WindowFlags_InternalModal : 0);
    bool visible = Begin(name.c_str(), p_open, popup_flags);
    if (!visible) {
        End(); GOG->cur_window=parent; PopupParents().pop_back(); PopupIDs().pop_back();
        if (modal && GOG->modal_window == existing) GOG->modal_window = NearestOpenModalParent(parent);
        return false;
    }
    Window* popup = GOG->cur_window;
    popup->popup_parent = parent;
    if (modal) {
        if (GOG->modal_window != popup) {
            GOG->active_id = GOG->text_active_id = GOG->nav_id = 0;
            GOG->active_id_window = GOG->text_active_id_window = 0; GOG->moving_window = 0;
            GOG->nav_activate_id = 0;
        }
        GOG->modal_window = popup;
        RestrictNavigationToWindow(*GOG, popup);
        FocusWindow(popup);
        GOG->io.want_capture_mouse = GOG->io.want_capture_keyboard = true;
    } else if (popup->appeared_this_frame) FocusWindow(popup);
    if (!GOG->text_active_id && IsKeyPressed(Key_Escape, false)) {
        PopupOpen()[popup_id] = false;
        if (modal && GOG->modal_window == popup) GOG->modal_window = NearestOpenModalParent(parent);
    }
    return visible;
}
bool BeginPopup(const char* id) { return BeginPopupEx(id,0,false); }
bool BeginPopupModal(const char* title, bool* p_open) {
    return BeginPopupEx(title,p_open,true);
}
void CloseCurrentPopup() {
    if (PopupIDs().empty()) return;
    PopupOpen()[PopupIDs().back()] = false;
    if (GOG->modal_window == GOG->cur_window)
        GOG->modal_window = NearestOpenModalParent(PopupParents().empty() ? 0 : PopupParents().back());
}
void EndPopup() {
    if (PopupParents().empty()) return;
    End(); GOG->cur_window = PopupParents().back(); PopupParents().pop_back(); PopupIDs().pop_back();
}

bool BeginDragDropSource() {
    if (!GOG || !GOG->cur_window || GOG->last_item.id == 0 || GOG->disabled_depth > 0) return false;
    if (!GOG->io.mouse_down[0] || (GOG->active_id != GOG->last_item.id && GOG->drag_payload.source_id != GOG->last_item.id)) return false;
    Vec2 drag=GetMouseDragDelta(0);if(!GOG->drag_drop_active&&drag.x*drag.x+drag.y*drag.y<9.0f)return false;
    GOG->drag_drop_active = true; GOG->drag_drop_delivered = false;
    GOG->drag_payload.source_id = GOG->last_item.id;
    return true;
}
bool SetDragDropPayload(const char* type, const void* data, int data_size) {
    if (!GOG || !GOG->drag_drop_active || !type || !*type || !data || data_size <= 0) return false;
    GOG->drag_payload_type = type;
    const unsigned char* begin = (const unsigned char*)data;
    GOG->drag_payload_data.assign(begin, begin + data_size);
    GOG->drag_payload.data = GOG->drag_payload_data.data(); GOG->drag_payload.data_size = data_size;
    return true;
}
void EndDragDropSource() {
    if (!GOG || !GOG->drag_drop_active) return;
    if (!GOG->drag_payload_type.empty()) SetTooltip(GOG->drag_payload_type.c_str());
}
bool BeginDragDropTarget() {
    if (!GOG || !GOG->drag_drop_active || GOG->last_item.id == 0 ||
        GOG->last_item.id == GOG->drag_payload.source_id || GOG->disabled_depth > 0 ||
        !(GOG->last_item.status_flags & ItemStatus_Visible) ||
        (GOG->last_item.status_flags & ItemStatus_Disabled)) return false;
    bool preview_hovered = IsItemHovered();
    bool released_here = GOG->mouse_released[0] && GOG->cur_window &&
                         ItemContainsPoint(GOG->last_item.rect, GOG->mouse_released_pos[0], GOG->cur_window);
    if (!preview_hovered && !released_here) return false;
    GOG->drag_drop_target = true;
    Window* w = GOG->cur_window; const Vec4& rect = GOG->last_item.rect;
    w->draw.AddRect(Vec2(rect.x,rect.y),Vec2(rect.z,rect.w),GetColorU32(Col_DragDropTarget),2.0f);
    return true;
}
const Payload* AcceptDragDropPayload(const char* type) {
    if (!GOG || !GOG->drag_drop_target || GOG->drag_drop_delivered || !type ||
        GOG->drag_payload_type != type || GOG->drag_payload_data.empty()) return 0;
    GOG->drag_payload.preview = true;
    GOG->drag_payload.delivery = GOG->mouse_released[0] && GOG->cur_window &&
        ItemContainsPoint(GOG->last_item.rect, GOG->mouse_released_pos[0], GOG->cur_window);
    if (GOG->drag_payload.delivery) {
        GOG->drag_drop_delivered = true;
        GOG->events.push_back(Event(Event_DragDropDelivered, GOG->last_item.id, type));
    }
    return &GOG->drag_payload;
}
void EndDragDropTarget() {
    if (!GOG) return; GOG->drag_drop_target = false;
}

struct TableState {
    Window* window; ID id; int columns, column, row, flags; float width, column_width, row_y, row_height;
    Vec2 outer_cursor, outer_start; float outer_content, outer_indent; TableSortSpec sort; bool clip_active;
    TableState() : window(0), id(0), columns(0), column(0), row(0), flags(0), width(0), column_width(0), row_y(0), row_height(0), clip_active(false) {}
};
static thread_local std::map<Context*, std::vector<TableState> > GTablesByContext;
static thread_local std::map<Context*, std::map<ID, TableSortSpec> > GTableSortsByContext;
static std::vector<TableState>& Tables() { return GTablesByContext[GOG]; }
static std::map<ID, TableSortSpec>& TableSorts() { return GTableSortsByContext[GOG]; }
static void TableSetColumn(TableState& t);
static void TableMeasureCell(TableState& t) {
    float extent = Max(0.0f, t.window->cursor.y - t.row_y - GOG->style.item_spacing.y);
    t.row_height = Max(t.row_height, Max(extent, t.window->prev_line_height));
}
bool BeginTable(const char* id, int columns, int flags) {
    if (!GOG || !GOG->cur_window || columns < 1) return false;
    Window* w = GOG->cur_window; TableState t; t.window=w; t.id=GetID(id); t.columns=columns; t.flags=flags;
    t.width=GetContentRegionAvail().x; t.column_width=t.width/columns; t.outer_cursor=w->cursor; t.outer_start=w->cursor_start;
    t.outer_content=w->content_w; t.outer_indent=w->indent; t.row_y=w->cursor.y; t.sort=TableSorts()[t.id];t.sort.dirty=false;TableSorts()[t.id].dirty=false; Tables().push_back(t);
    w->id_stack.push_back(t.id); w->content_w=t.column_width; w->cursor_start=w->cursor; w->indent=0; TableSetColumn(Tables().back()); return true;
}
static void TableSetColumn(TableState& t) {
    Window* w=t.window; float x=t.outer_cursor.x+t.column*t.column_width;
    w->cursor_start=Vec2(x,t.row_y); w->cursor=w->cursor_start; w->cursor_prev_line=w->cursor;
    w->curr_line_height=w->prev_line_height=0.0f; w->content_w=t.column_width; w->indent=0;
    w->draw.PushClipRect(Vec4(x,t.outer_cursor.y,x+t.column_width,t.window->pos.y+t.window->size.y));t.clip_active=true;
}
bool TableHeader(const char* label) {
    if (Tables().empty()) return false; TableState& t=Tables().back(); int col=t.column;
    const char* marker=(t.sort.column==col?(t.sort.direction==Sort_Ascending?"  ^":t.sort.direction==Sort_Descending?"  v":""):"");
    std::string text=label?label:"";text+=marker;text+="###table_header";PushID(col);bool clicked=(t.flags&TableFlags_Sortable)?Button(text.c_str(),Vec2(t.column_width-2,0)):Selectable(text.c_str(),false,Vec2(t.column_width-2,0));PopID();
    if (clicked && (t.flags&TableFlags_Sortable)) {
        if (t.sort.column != col || t.sort.direction == Sort_None) {
            t.sort.column = col; t.sort.direction = Sort_Ascending;
        } else t.sort.direction = t.sort.direction == Sort_Ascending ? Sort_Descending : Sort_Ascending;
        t.sort.dirty=true; TableSorts()[t.id]=t.sort;
    }
    TableNextColumn(); return clicked;
}
void TableNextColumn() {
    if (Tables().empty()) return; TableState& t=Tables().back(); Window* w=t.window;if(t.clip_active){w->draw.PopClipRect();t.clip_active=false;}
    TableMeasureCell(t); t.row_height=Max(t.row_height,GOG->atlas.line_height+GOG->style.frame_padding.y*2);
    ++t.column; if(t.column>=t.columns){t.column=0;++t.row;t.row_y+=t.row_height+2;t.row_height=0;} TableSetColumn(t);
}
void TableNextRow() {
    if(Tables().empty())return; TableState& t=Tables().back();if(t.clip_active){t.window->draw.PopClipRect();t.clip_active=false;}
    if(t.column!=0){TableMeasureCell(t);t.column=0;++t.row;t.row_y+=Max(t.row_height,GOG->atlas.line_height+8)+2;t.row_height=0;} TableSetColumn(t);
}
bool TableSelectable(const char* label, bool selected) {
    if(Tables().empty())return false; TableState& t=Tables().back();Vec2 pos=t.window->cursor;float h=GOG->atlas.line_height+GOG->style.frame_padding.y*2;
    if((t.flags&TableFlags_RowBg)&&(t.row&1))t.window->draw.AddRectFilled(pos,Vec2(pos.x+t.column_width-2,pos.y+h),ColorWithAlpha(GetColorU32(Col_Header),90));
    PushID(t.row*t.columns+t.column);bool clicked=Selectable(label,selected,Vec2(t.column_width-2,0));PopID();TableNextColumn();return clicked;
}
const TableSortSpec* TableGetSortSpec(){return Tables().empty()?0:&Tables().back().sort;}
void EndTable() {
    if(Tables().empty())return; TableState& current=Tables().back();TableMeasureCell(current);TableState t=current;Tables().pop_back();Window* w=t.window;if(t.clip_active)w->draw.PopClipRect();
    float total=(t.row_y-t.outer_cursor.y)+(t.row_height>0.0f?Max(t.row_height,GOG->atlas.line_height+8):0.0f);
    if(t.flags&TableFlags_Borders){w->draw.AddRect(t.outer_cursor,Vec2(t.outer_cursor.x+t.width,t.outer_cursor.y+total),GetColorU32(Col_Border),1);for(int c=1;c<t.columns;++c){float x=t.outer_cursor.x+c*t.column_width;w->draw.AddLine(Vec2(x,t.outer_cursor.y),Vec2(x,t.outer_cursor.y+total),GetColorU32(Col_Border),1);}}
    if(w->id_stack.size()>1)w->id_stack.pop_back();w->cursor_start=t.outer_start;w->content_w=t.outer_content;w->indent=t.outer_indent;w->cursor=t.outer_cursor;ItemSize(Vec2(t.width,total));
}

static void PlotImpl(const char* label, const float* values, int count, Vec2 size_arg, bool histogram) {
    if (!GOG || !GOG->cur_window || !label) return;
    Window* w = GOG->cur_window;
    Style& s = GOG->style;
    const char* end = FindDisplayEnd(label);
    float fw = Max(1.0f, size_arg.x > 0 ? size_arg.x : w->content_w * 0.62f);
    float fh = Max(1.0f, size_arg.y > 0 ? size_arg.y : 60.0f);
    Vec2 pos = w->cursor;
    Vec4 r(pos.x, pos.y, pos.x + fw, pos.y + fh);
    Vec2 ls = CalcTextSize(label, end);
    ItemSize(Vec2(fw + s.item_inner_spacing.x + ls.x, fh));
    w->draw.AddRectFilled(pos, Vec2(r.z, r.w), GetColorU32(Col_FrameBg));
    if (values && count > 0) {
        float vmin = 0.0f, vmax = 0.0f; bool have_value = false;
        for (int i = 0; i < count; ++i) {
            if (!FiniteFloat(values[i])) continue;
            if (!have_value) { vmin = vmax = values[i]; have_value = true; }
            else { if (values[i] < vmin) vmin = values[i]; if (values[i] > vmax) vmax = values[i]; }
        }
        if (!have_value) {
            w->draw.AddText(Vec2(r.z + s.item_inner_spacing.x, pos.y + (fh - GOG->atlas.line_height) * 0.5f), GetColorU32(Col_Text), label, end);
            return;
        }
        if (vmax <= vmin) vmax = vmin + 1.0f;
        float inner = Max(1.0f, fh - 4);
        if (histogram) {
            U32 col = GetColorU32(Col_PlotHistogram);
            float bw = fw / count;
            for (int i = 0; i < count; i++) {
                if (!FiniteFloat(values[i])) continue;
                float t = (values[i] - vmin) / (vmax - vmin);
                float bx = r.x + i * bw;
                float inset = bw > 2.0f ? 1.0f : 0.0f;
                w->draw.AddRectFilled(Vec2(bx + inset, r.w - 2 - t * inner), Vec2(bx + Max(inset + 0.5f, bw - inset), r.w - 2), col);
            }
        } else {
            U32 col = GetColorU32(Col_PlotLines);
            for (int i = 0; i < count - 1; i++) {
                if (!FiniteFloat(values[i]) || !FiniteFloat(values[i + 1])) continue;
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
static thread_local std::map<Context*, ChartBuildState> GCharts;
static ChartBuildState& Chart(){return GCharts[GOG];}

bool BeginChart(const char* label, const Vec2& size_arg) {
    if (!GOG || !GOG->cur_window || Chart().active) return false;
    Window* w = GOG->cur_window;
    const char* chart_label = label ? label : "Chart";
    float width = Max(80.0f, size_arg.x > 0.0f ? size_arg.x : GetContentRegionAvail().x);
    float height = Max(80.0f, size_arg.y > 0.0f ? size_arg.y : 150.0f);
    Vec2 pos = w->cursor;
    Chart().active = true;
    Chart().window = w;
    Chart().id = GetID(chart_label);
    Chart().label = chart_label;
    Chart().rect = Vec4(pos.x, pos.y, pos.x + width, pos.y + height);
    Chart().series.clear();
    ItemSize(Vec2(width, height));
    if (GOG->submitted_ids.find(Chart().id) != GOG->submitted_ids.end()) ++GOG->metrics.id_conflicts;
    else GOG->submitted_ids[Chart().id] = Chart().rect;
    GOG->last_item.id = Chart().id; GOG->last_item.rect = Chart().rect;
    if (ItemHoverable(Chart().rect, Chart().id)) GOG->last_item.status_flags |= ItemStatus_Hovered;
    return true;
}
static void ChartAddSeries(const char* label, const float* values, int count, U32 color, bool bars) {
    if (!Chart().active || !values || count <= 0) return;
    ChartSeriesData data;
    data.label = label ? label : "Series";
    data.values.reserve((size_t)count);float previous=0.0f;
    for(int i=0;i<count;++i){float value=FiniteFloat(values[i])?values[i]:previous;data.values.push_back(value);previous=value;}
    data.color = color;
    data.type = bars ? 1 : 0;
    Chart().series.push_back(data);
}
void ChartLine(const char* label, const float* values, int count, U32 color) {
    ChartAddSeries(label, values, count, color, false);
}
void ChartLine(const char* label, const StreamingSeries& series, U32 color) {
    if (!Chart().active || series.Size() <= 0) return;
    ChartSeriesData data;
    data.label = label ? label : "Series";
    series.GetOrdered(data.values);
    data.color = color;
    data.type = 0;
    Chart().series.push_back(data);
}
void ChartBars(const char* label, const float* values, int count, U32 color) {
    ChartAddSeries(label, values, count, color, true);
}
void ChartArea(const char* label, const float* values, int count, U32 color) {
    size_t previous = Chart().series.size(); ChartAddSeries(label, values, count, color, false);
    if (Chart().series.size() > previous) Chart().series.back().type = 2;
}
void ChartScatter(const char* label, const Vec2* points, int count, U32 color) {
    if (!Chart().active || !points || count <= 0) return;
    ChartSeriesData d; d.label=label?label:"Scatter"; d.color=color; d.type=3;
    for(int i=0;i<count;++i)if(FiniteFloat(points[i].x)&&FiniteFloat(points[i].y))d.points.push_back(points[i]);
    if(!d.points.empty())Chart().series.push_back(d);
}
void ChartPie(const char* label, const float* values, const char* const labels[], int count) {
    if (!Chart().active || !values || count<=0)return;ChartSeriesData d;d.label=label?label:"Pie";d.color=0;d.type=4;
    for(int i=0;i<count;++i){if(!FiniteFloat(values[i])||values[i]<0.0f)continue;d.values.push_back(values[i]);d.slice_labels.push_back(labels&&labels[i]?labels[i]:"");}
    if(!d.values.empty())Chart().series.push_back(d);
}
void ChartCandlesticks(const char* label, const Candlestick* values, int count) {
    if(!Chart().active||!values||count<=0)return;ChartSeriesData d;d.label=label?label:"OHLC";d.color=0;d.type=5;
    for(int i=0;i<count;++i){Candlestick c=values[i];if(!FiniteFloat(c.open)||!FiniteFloat(c.high)||!FiniteFloat(c.low)||!FiniteFloat(c.close))continue;
        if(c.high<c.low){float swap=c.high;c.high=c.low;c.low=swap;}c.high=Max(c.high,Max(c.open,c.close));c.low=Min(c.low,Min(c.open,c.close));d.candles.push_back(c);}
    if(!d.candles.empty())Chart().series.push_back(d);
}
void EndChart() {
    if (!Chart().active || !Chart().window) return;
    Window* w = Chart().window;
    const Style& s = GOG->style;
    Vec4 r = Chart().rect;
    w->draw.AddRectFilledRounded(Vec2(r.x, r.y), Vec2(r.z, r.w), GetColorU32(Col_FrameBg), s.frame_rounding + 2.0f);
    w->draw.PushClipRect(r);
    w->draw.AddText(Vec2(r.x + 12, r.y + 9), GetColorU32(Col_Text), Chart().label.c_str());

    Vec4 plot(r.x + 12, r.y + 34, r.z - 12, r.w - 12);
    for (int i = 1; i < 4; ++i) {
        float y = plot.y + (plot.w - plot.y) * (float)i / 4.0f;
        w->draw.AddLine(Vec2(plot.x, y), Vec2(plot.z, y), ColorWithAlpha(GetColorU32(Col_Separator), 75), 1.0f);
    }

    float vmin = 0.0f, vmax = 1.0f;
    bool first = true;
    for (size_t n = 0; n < Chart().series.size(); ++n) {
        const ChartSeriesData& series = Chart().series[n];
        if (series.type == 4) continue;
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
    float zero_y = plot.w - (0.0f - vmin) / (vmax - vmin) * (plot.w - plot.y);

    const U32 palette[] = {
        GetColorU32(Col_PlotLines), GetColorU32(Col_GradientStart),
        GetColorU32(Col_Warning), GetColorU32(Col_Link)
    };
    for (size_t n = 0; n < Chart().series.size(); ++n) {
        const ChartSeriesData& data = Chart().series[n];
        int count = (int)data.values.size();
        U32 color = data.color ? data.color : palette[n % 4];
        if (data.type == 4 && count > 0) {
            double sum=0.0;for(int i=0;i<count;++i)sum+=(double)data.values[(size_t)i];if(!(sum>0.0)||sum>DBL_MAX)sum=1.0;
            Vec2 center((plot.x+plot.z)*0.5f,(plot.y+plot.w)*0.5f);float radius=Min(plot.z-plot.x,plot.w-plot.y)*0.38f;float angle=-1.5707963f;
            for(int i=0;i<count;++i){float sweep=(float)((double)data.values[(size_t)i]/sum*6.28318530718);U32 slice=palette[i%4];int segs=(int)(sweep*14.0f);if(segs<2)segs=2;if(segs>96)segs=96;for(int q=0;q<segs;++q){float a0=angle+sweep*(float)q/(float)segs,a1=angle+sweep*(float)(q+1)/(float)segs;w->draw.AddTriangleFilled(center,Vec2(center.x+cosf(a0)*radius,center.y+sinf(a0)*radius),Vec2(center.x+cosf(a1)*radius,center.y+sinf(a1)*radius),slice);}angle+=sweep;}
            float slice_legend_y=plot.y+3.0f;for(int i=0;i<count&&slice_legend_y+GOG->atlas.line_height<=plot.w;++i){if((size_t)i>=data.slice_labels.size()||data.slice_labels[(size_t)i].empty())continue;w->draw.AddCircleFilled(Vec2(plot.x+5.0f,slice_legend_y+GOG->atlas.line_height*.5f),3.0f,palette[i%4],10);w->draw.AddText(Vec2(plot.x+12.0f,slice_legend_y),GetColorU32(Col_TextDisabled),data.slice_labels[(size_t)i].c_str());slice_legend_y+=GOG->atlas.line_height+2.0f;}
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
                float inset = bw > 2.0f ? 1.0f : 0.0f;
                float value_y=plot.w-t*(plot.w-plot.y);
                w->draw.AddRectFilledRounded(Vec2(x0+inset,Min(value_y,zero_y)),
                                             Vec2(x0+Max(inset+0.5f,bw-inset),Max(value_y,zero_y)+0.5f),ColorWithAlpha(color,210),2.0f);
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
                int minimum=i,maximum=i;for(int j=i+1;j<end;++j){if(data.values[(size_t)j]<data.values[(size_t)minimum])minimum=j;if(data.values[(size_t)j]>data.values[(size_t)maximum])maximum=j;}
                int sample_indices[2]={minimum,maximum};if(sample_indices[1]<sample_indices[0]){int swap=sample_indices[0];sample_indices[0]=sample_indices[1];sample_indices[1]=swap;}
                int samples=minimum==maximum?1:2;for(int sample=0;sample<samples;++sample){int index=sample_indices[sample];float value=data.values[(size_t)index];float x=plot.x+(float)index/(count-1)*(plot.z-plot.x);float t=(value-vmin)/(vmax-vmin);Vec2 p(x,plot.w-t*(plot.w-plot.y));
                    if (have_previous) {
                        w->draw.AddQuad(previous, p, Vec2(p.x, zero_y), Vec2(previous.x, zero_y), ColorWithAlpha(color, data.type == 2 ? 70 : 24));
                        w->draw.AddLine(previous, p, color, 2.0f);
                    }
                    previous=p;have_previous=true;
                }
            }
        }
    }

    float legend_x = r.z - 12.0f;
    for (int n = (int)Chart().series.size() - 1; n >= 0; --n) {
        const ChartSeriesData& data = Chart().series[(size_t)n];
        Vec2 text_size = CalcTextSize(data.label.c_str());
        legend_x -= text_size.x;
        U32 color = data.color ? data.color : palette[n % 4];
        w->draw.AddText(Vec2(legend_x, r.y + 9), GetColorU32(Col_TextDisabled), data.label.c_str());
        legend_x -= 12.0f;
        w->draw.AddCircleFilled(Vec2(legend_x + 4, r.y + 16), 3.0f, color, 12);
        legend_x -= 10.0f;
    }

    w->draw.PopClipRect();
    Chart().active = false;
    Chart().window = 0;
    Chart().series.clear();
}

// =====================================================================
//  Markdown
// =====================================================================
struct MarkdownConfig { MarkdownLinkCallback link; MarkdownImageResolver image; void* link_user; void* image_user; MarkdownConfig():link(0),image(0),link_user(0),image_user(0){} };
static thread_local std::map<Context*,MarkdownConfig> GMarkdownConfigs;
static MarkdownConfig& MarkdownConfiguration(){return GMarkdownConfigs[GOG];}
void SetMarkdownLinkCallback(MarkdownLinkCallback callback, void* user_data) { if(GOG){MarkdownConfiguration().link=callback;MarkdownConfiguration().link_user=user_data;} }
void SetMarkdownImageResolver(MarkdownImageResolver resolver, void* user_data) { if(GOG){MarkdownConfiguration().image=resolver;MarkdownConfiguration().image_user=user_data;} }

static bool MarkdownInteractiveLine(const std::string& line) {
    size_t open=line.find('['); if(open==std::string::npos)return false;
    bool image=open>0&&line[open-1]=='!'; size_t close=line.find(']',open+1);
    if(close==std::string::npos||close+1>=line.size()||line[close+1]!='(')return false;
    size_t finish=line.find(')',close+2);if(finish==std::string::npos)return false;
    std::string caption=line.substr(open+1,close-open-1),url=line.substr(close+2,finish-close-2);
    if(image){
        TextureID tex=0;Vec2 size(0,0);
        if(MarkdownConfiguration().image&&MarkdownConfiguration().image(url.c_str(),&tex,&size,MarkdownConfiguration().image_user)&&tex&&size.x>0&&size.y>0){Image(tex,size);return true;}
        GlassCard((std::string("Image: ")+caption).c_str(),Vec2(-1,64),4);return true;
    }
    Window* w=GOG->cur_window;Vec2 pos=w->cursor;std::string prefix=open>0?line.substr(0,open):std::string();
    std::string suffix=finish+1<line.size()?line.substr(finish+1):std::string();float prefix_width=CalcTextSize(prefix.c_str()).x;
    Vec2 link_size=CalcTextSize(caption.c_str());float total_width=prefix_width+link_size.x+CalcTextSize(suffix.c_str()).x;
    ItemSize(Vec2(total_width,GOG->atlas.line_height));float x=pos.x+prefix_width;ID id=HashStr(url.c_str(),0,w->id_stack.back());Vec4 r(x,pos.y,x+link_size.x,pos.y+link_size.y);
    if(!w->draw.clip_stack.empty()){const Vec4& clip=w->draw.clip_stack.back();if(r.z<=clip.x||r.x>=clip.z||r.w<=clip.y||r.y>=clip.w)GOG->last_item.status_flags&=~ItemStatus_Visible;}
    bool hovered=false,held=false,pressed=ButtonBehavior(r,id,&hovered,&held);w->draw.AddText(Vec2(x,pos.y),GetColorU32(Col_Link),caption.c_str());
    if(!prefix.empty())w->draw.AddText(pos,GetColorU32(Col_Text),prefix.c_str());
    w->draw.AddLine(Vec2(x,r.w),Vec2(r.z,r.w),ColorWithAlpha(GetColorU32(Col_Link),hovered?255:120),1);
    if(pressed){GOG->events.push_back(Event(Event_LinkActivated,id,url.c_str()));if(MarkdownConfiguration().link)MarkdownConfiguration().link(url.c_str(),MarkdownConfiguration().link_user);}
    x=r.z;if(!suffix.empty())w->draw.AddText(Vec2(x,pos.y),GetColorU32(Col_Text),suffix.c_str());return true;
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
    Window* markdown_window=GOG->cur_window;ID parent_seed=markdown_window->id_stack.empty()?0:markdown_window->id_stack.back();
    ID content_seed=HashStr(markdown,0,parent_seed);int call_ordinal=GOG->markdown_call_counts[content_seed]++;char call_scope[48];
    snprintf(call_scope,sizeof(call_scope),"markdown-call:%d",call_ordinal);markdown_window->id_stack.push_back(HashStr(call_scope,0,content_seed));
    bool code = false;
    size_t start = 0;
    int line_number = 0;
    while (start <= source.size()) {
        size_t end = source.find('\n', start);
        if (end == std::string::npos) end = source.size();
        std::string line = source.substr(start, end - start);
        if (!line.empty() && line[line.size() - 1] == '\r') line.erase(line.size() - 1);
        PushID(line_number++);

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
        PopID();
        if (end == source.size()) break;
        start = end + 1;
    }
    if(markdown_window->id_stack.size()>1)markdown_window->id_stack.pop_back();
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
    int input_index;
    int output_index;
    NodeEditorState() : active(false), node_active(false), window(0), editor_id(0), node_id(0), input_y(0), output_y(0), input_index(0), output_index(0) {}
};
static thread_local std::map<Context*,NodeEditorState> GNodeEditors;
static NodeEditorState& NodeEditor(){return GNodeEditors[GOG];}

bool BeginNodeEditor(const char* id, const Vec2& size_arg) {
    if (!GOG || !GOG->cur_window || NodeEditor().active) return false;
    Window* w = GOG->cur_window;
    float width = Max(80.0f, size_arg.x > 0.0f ? size_arg.x : GetContentRegionAvail().x);
    float height = Max(80.0f, size_arg.y > 0.0f ? size_arg.y : 300.0f);
    Vec2 pos = w->cursor;
    ItemSize(Vec2(width, height));

    NodeEditor().active = true;
    NodeEditor().node_active = false;
    NodeEditor().window = w;
    NodeEditor().editor_id = GetID(id ? id : "node-editor");
    NodeEditor().rect = Vec4(pos.x, pos.y, pos.x + width, pos.y + height);
    if (GOG->submitted_ids.find(NodeEditor().editor_id) != GOG->submitted_ids.end()) ++GOG->metrics.id_conflicts;
    else GOG->submitted_ids[NodeEditor().editor_id] = NodeEditor().rect;
    GOG->last_item.id = NodeEditor().editor_id; GOG->last_item.rect = NodeEditor().rect;
    if (ItemHoverable(NodeEditor().rect, NodeEditor().editor_id)) GOG->last_item.status_flags |= ItemStatus_Hovered;
    w->id_stack.push_back(NodeEditor().editor_id);

    w->draw.AddRectFilledRounded(pos, Vec2(pos.x + width, pos.y + height), GetColorU32(Col_CodeBg), GOG->style.frame_rounding + 3.0f);
    w->draw.PushClipRect(NodeEditor().rect);
    const float grid = 24.0f;
    for (float x = pos.x; x < pos.x + width; x += grid)
        w->draw.AddLine(Vec2(x, pos.y), Vec2(x, pos.y + height), GetColorU32(Col_NodeGrid), 1.0f);
    for (float y = pos.y; y < pos.y + height; y += grid)
        w->draw.AddLine(Vec2(pos.x, y), Vec2(pos.x + width, y), GetColorU32(Col_NodeGrid), 1.0f);
    return true;
}

bool BeginNode(int node_id, const char* title, Vec2* position, const Vec2& size_arg) {
    if (!NodeEditor().active || NodeEditor().node_active || !position) return false;
    Window* w = NodeEditor().window;
    char id_buf[48];
    snprintf(id_buf, sizeof(id_buf), "node:%d", node_id);
    ID id = HashStr(id_buf, 0, NodeEditor().editor_id);
    if (!FiniteFloat(position->x)) position->x = 0.0f;
    if (!FiniteFloat(position->y)) position->y = 0.0f;
    Vec2 pos(NodeEditor().rect.x + position->x, NodeEditor().rect.y + position->y);
    Vec2 size(Max(size_arg.x, 120.0f), Max(size_arg.y, 80.0f));
    Vec4 title_rect(pos.x, pos.y, pos.x + size.x, pos.y + 34.0f);

    bool hovered = false, held = false;
    ButtonBehavior(title_rect, id, &hovered, &held);
    if (held) {
        position->x += GOG->io.mouse_pos.x - GOG->mouse_pos_prev.x;
        position->y += GOG->io.mouse_pos.y - GOG->mouse_pos_prev.y;
        position->x = Clamp(position->x, 0.0f, Max(0.0f, (NodeEditor().rect.z - NodeEditor().rect.x) - size.x));
        position->y = Clamp(position->y, 0.0f, Max(0.0f, (NodeEditor().rect.w - NodeEditor().rect.y) - size.y));
        pos = Vec2(NodeEditor().rect.x + position->x, NodeEditor().rect.y + position->y);
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

    NodeEditor().node_active = true;
    NodeEditor().node_id = id;
    NodeEditor().node_rect = Vec4(pos.x, pos.y, end.x, end.y);
    NodeEditor().input_y = pos.y + 55.0f;
    NodeEditor().output_y = pos.y + 55.0f;
    NodeEditor().input_index = NodeEditor().output_index = 0;
    return true;
}

NodePin NodeInput(const char* label) {
    if (!NodeEditor().node_active) return NodePin();
    Window* w = NodeEditor().window;
    Vec2 pin(NodeEditor().node_rect.x, NodeEditor().input_y);
    ID ordinal = (ID)++NodeEditor().input_index * 0x9E3779B97F4A7C15ULL;
    ID id = HashStr(label ? label : "input", 0, NodeEditor().node_id ^ 0x1A2B3C4DULL ^ ordinal);
    w->draw.AddCircleFilled(pin, 6.0f, GetColorU32(Col_Success), 16);
    w->draw.AddCircleFilled(pin, 2.5f, GetColorU32(Col_WindowBg), 12);
    w->draw.AddText(Vec2(pin.x + 12.0f, pin.y - GOG->atlas.line_height * 0.5f), GetColorU32(Col_Text), label ? label : "Input");
    NodeEditor().input_y += 24.0f;
    return NodePin(id, pin);
}

NodePin NodeOutput(const char* label) {
    if (!NodeEditor().node_active) return NodePin();
    Window* w = NodeEditor().window;
    Vec2 pin(NodeEditor().node_rect.z, NodeEditor().output_y);
    ID ordinal = (ID)++NodeEditor().output_index * 0x9E3779B97F4A7C15ULL;
    ID id = HashStr(label ? label : "output", 0, NodeEditor().node_id ^ 0x5E6F7788ULL ^ ordinal);
    Vec2 ts = CalcTextSize(label ? label : "Output");
    w->draw.AddText(Vec2(pin.x - 12.0f - ts.x, pin.y - GOG->atlas.line_height * 0.5f), GetColorU32(Col_Text), label ? label : "Output");
    w->draw.AddCircleFilled(pin, 6.0f, GetColorU32(Col_NodeLink), 16);
    w->draw.AddCircleFilled(pin, 2.5f, GetColorU32(Col_WindowBg), 12);
    NodeEditor().output_y += 24.0f;
    return NodePin(id, pin);
}

void EndNode() { NodeEditor().node_active = false; }

void NodeLink(const NodePin& from, const NodePin& to, U32 color) {
    if (!NodeEditor().active || from.id == 0 || to.id == 0) return;
    if (!FiniteFloat(from.position.x) || !FiniteFloat(from.position.y) ||
        !FiniteFloat(to.position.x) || !FiniteFloat(to.position.y)) return;
    DrawList& draw = NodeEditor().window->draw;
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
    if (!NodeEditor().active) return;
    Window* w = NodeEditor().window;
    if (NodeEditor().node_active) EndNode();
    w->draw.PopClipRect();
    if (w->id_stack.size() > 1) w->id_stack.pop_back();
    NodeEditor().active = false;
    NodeEditor().window = 0;
}

static std::string JSONEscape(const std::string& value) {
    std::string out;
    out.reserve(value.size() + 8);
    for (size_t i = 0; i < value.size(); ++i) {
        unsigned char c = (unsigned char)value[i];
        switch (c) {
            case '\\': out += "\\\\"; break;
            case '"':  out += "\\\""; break;
            case '\n': out += "\\n"; break;
            case '\r': out += "\\r"; break;
            case '\t': out += "\\t"; break;
            case '\b': out += "\\b"; break;
            case '\f': out += "\\f"; break;
            default:
                if (c < 0x20) {
                    char escaped[8]; snprintf(escaped, sizeof(escaped), "\\u%04x", (unsigned)c); out += escaped;
                } else out.push_back((char)c);
                break;
        }
    }
    return out;
}
static int JSONHexValue(char value) {
    if (value >= '0' && value <= '9') return value - '0';
    if (value >= 'a' && value <= 'f') return value - 'a' + 10;
    if (value >= 'A' && value <= 'F') return value - 'A' + 10;
    return -1;
}
static bool JSONHexCodepoint(const std::string& value, size_t offset, unsigned int& codepoint) {
    if (offset + 4 > value.size()) return false;
    codepoint = 0;
    for (size_t i = 0; i < 4; ++i) {
        int digit = JSONHexValue(value[offset + i]);
        if (digit < 0) return false;
        codepoint = codepoint * 16u + (unsigned int)digit;
    }
    return true;
}
static bool JSONUnescape(const std::string& value, std::string& out) {
    out.clear();
    out.reserve(value.size());
    for (size_t i = 0; i < value.size(); ++i) {
        unsigned char raw = (unsigned char)value[i];
        if (raw < 0x20) return false;
        if (value[i] != '\\') { out.push_back(value[i]); continue; }
        if (i + 1 >= value.size()) return false;
        char escape = value[++i];
        if (escape == 'n') out.push_back('\n');
        else if (escape == 'r') out.push_back('\r');
        else if (escape == 't') out.push_back('\t');
        else if (escape == 'b') out.push_back('\b');
        else if (escape == 'f') out.push_back('\f');
        else if (escape == '\\' || escape == '"' || escape == '/') out.push_back(escape);
        else if (escape == 'u') {
            unsigned int codepoint = 0;
            if (!JSONHexCodepoint(value, i + 1, codepoint)) return false;
            i += 4;
            if (codepoint >= 0xD800 && codepoint <= 0xDBFF) {
                if (i + 6 >= value.size() || value[i + 1] != '\\' || value[i + 2] != 'u')
                    return false;
                unsigned int low = 0;
                if (!JSONHexCodepoint(value, i + 3, low) || low < 0xDC00 || low > 0xDFFF)
                    return false;
                codepoint = 0x10000u + ((codepoint - 0xD800u) << 10) + (low - 0xDC00u);
                i += 6;
            } else if (codepoint >= 0xDC00 && codepoint <= 0xDFFF) {
                return false;
            }
            char bytes[5]; int count = EncodeUTF8(codepoint, bytes); out.append(bytes, (size_t)count);
        } else return false;
    }
    return true;
}
static size_t FindJSONStringEnd(const std::string& source, size_t opening_quote, size_t limit) {
    bool escaped = false;
    for (size_t i = opening_quote + 1; i < source.size() && i < limit; ++i) {
        if (escaped) { escaped = false; continue; }
        if (source[i] == '\\') { escaped = true; continue; }
        if (source[i] == '"') return i;
    }
    return std::string::npos;
}
static size_t FindJSONContainerEnd(const std::string& source, size_t opening,
                                   size_t limit, char expected_opening) {
    if (opening >= source.size() || opening >= limit || source[opening] != expected_opening)
        return std::string::npos;
    std::vector<char> stack;
    bool in_string = false, escaped = false;
    for (size_t i = opening; i < source.size() && i < limit; ++i) {
        char c = source[i];
        if (in_string) {
            if (escaped) escaped = false;
            else if (c == '\\') escaped = true;
            else if (c == '"') in_string = false;
            continue;
        }
        if (c == '"') in_string = true;
        else if (c == '{' || c == '[') stack.push_back(c);
        else if (c == '}' || c == ']') {
            if (stack.empty()) return std::string::npos;
            char expected_closing = stack.back() == '{' ? '}' : ']';
            if (c != expected_closing) return std::string::npos;
            stack.pop_back();
            if (stack.empty()) return i;
        }
    }
    return std::string::npos;
}
static size_t FindJSONObjectEnd(const std::string& source, size_t opening_brace, size_t limit) {
    return FindJSONContainerEnd(source, opening_brace, limit, '{');
}
static size_t FindJSONArrayEnd(const std::string& source, size_t opening_bracket, size_t limit) {
    return FindJSONContainerEnd(source, opening_bracket, limit, '[');
}
static size_t FindJSONMemberColon(const std::string& source, const char* key,
                                  size_t object_start = 0, size_t object_limit = std::string::npos,
                                  bool* duplicate = 0) {
    if (!key) return std::string::npos;
    if (duplicate) *duplicate = false;
    size_t limit = object_limit == std::string::npos || object_limit > source.size()
        ? source.size() : object_limit;
    size_t opening_brace = source.find('{', object_start);
    if (opening_brace == std::string::npos || opening_brace >= limit) return std::string::npos;
    size_t found = std::string::npos;
    int object_depth = 0;
    int array_depth = 0;
    for (size_t position = opening_brace; position < limit; ++position) {
        char character = source[position];
        if (character == '"') {
            size_t quote_end = FindJSONStringEnd(source, position, limit);
            if (quote_end == std::string::npos) return std::string::npos;
            if (object_depth == 1 && array_depth == 0 &&
                source.compare(position + 1, quote_end - position - 1, key) == 0) {
                size_t colon = quote_end + 1;
                while (colon < limit && (source[colon] == ' ' || source[colon] == '\t' ||
                       source[colon] == '\r' || source[colon] == '\n')) ++colon;
                if (colon < limit && source[colon] == ':') {
                    if (found != std::string::npos) {
                        if (duplicate) *duplicate = true;
                    } else found = colon;
                }
            }
            position = quote_end;
            continue;
        }
        if (character == '{') ++object_depth;
        else if (character == '}') {
            --object_depth;
            if (object_depth == 0) break;
        } else if (character == '[') ++array_depth;
        else if (character == ']' && array_depth > 0) --array_depth;
    }
    return found;
}
static size_t SkipJSONWhitespace(const std::string& source, size_t position, size_t limit) {
    if (limit == std::string::npos || limit > source.size()) limit = source.size();
    while (position < limit && (source[position] == ' ' || source[position] == '\t' ||
           source[position] == '\r' || source[position] == '\n')) ++position;
    return position;
}
static bool IsJSONMemberValueTerminated(const std::string& source, size_t position, size_t limit) {
    position = SkipJSONWhitespace(source, position, limit);
    return position >= limit || source[position] == ',' || source[position] == '}';
}
static int JSONOptionalContainer(const std::string& source, const char* key, char opening,
                                 size_t& value_start, size_t& value_limit,
                                 size_t object_start = 0, size_t object_limit = std::string::npos) {
    size_t limit = object_limit == std::string::npos || object_limit > source.size()
        ? source.size() : object_limit;
    bool duplicate = false;
    size_t colon = FindJSONMemberColon(source, key, object_start, limit, &duplicate);
    if (colon == std::string::npos) return 0;
    if (duplicate) return -1;
    value_start = SkipJSONWhitespace(source, colon + 1, limit);
    if (value_start >= limit || source[value_start] != opening) return -1;
    size_t value_end = opening == '{'
        ? FindJSONObjectEnd(source, value_start, limit)
        : FindJSONArrayEnd(source, value_start, limit);
    if (value_end == std::string::npos || !IsJSONMemberValueTerminated(source, value_end + 1, limit))
        return -1;
    value_limit = value_end + 1;
    return 1;
}
static FILE* OpenFilePortable(const char* path, const char* mode) {
#ifdef _MSC_VER
    FILE* file = 0; return fopen_s(&file, path, mode) == 0 ? file : 0;
#else
    return fopen(path, mode);
#endif
}
std::string SaveStateToMemory(){
    if (!GOG) return std::string();
    const Style& style = GOG->style;
    std::ostringstream out;
    out.imbue(std::locale::classic());
    out << std::fixed << std::setprecision(4);
    out << "{\n  \"version\": 2,\n"
        << "  \"uiScale\": " << GOG->ui_scale << ",\n"
        << "  \"themePreset\": " << (int)GOG->theme_preset << ",\n"
        << "  \"reducedMotion\": " << (GOG->io.config_reduced_motion ? "true" : "false") << ",\n"
        << "  \"theme\": {\n"
        << "    \"windowPaddingX\": " << style.window_padding.x << ", \"windowPaddingY\": " << style.window_padding.y << ",\n"
        << "    \"framePaddingX\": " << style.frame_padding.x << ", \"framePaddingY\": " << style.frame_padding.y << ",\n"
        << "    \"itemSpacingX\": " << style.item_spacing.x << ", \"itemSpacingY\": " << style.item_spacing.y << ",\n"
        << "    \"itemInnerSpacingX\": " << style.item_inner_spacing.x << ", \"itemInnerSpacingY\": " << style.item_inner_spacing.y << ",\n"
        << "    \"indentSpacing\": " << style.indent_spacing << ", \"scrollbarSize\": " << style.scrollbar_size
        << ", \"grabMinSize\": " << style.grab_min_size << ",\n"
        << "    \"windowRounding\": " << style.window_rounding << ", \"frameRounding\": " << style.frame_rounding
        << ", \"shadowSize\": " << style.shadow_size << ",\n"
        << "    \"animationSpeed\": " << style.animation_speed << ", \"disabledAlpha\": " << style.disabled_alpha
        << ", \"motionScale\": " << style.motion_scale << ",\n"
        << "    \"colors\": [";
    for (int i = 0; i < Col_COUNT; ++i) { if (i) out << ", "; out << style.colors[i]; }
    out << "]\n  },\n  \"windows\": [\n";
    bool first = true;
    for (size_t i = 0; i < GOG->windows.size(); ++i) {
        Window* window = GOG->windows[i];
        if (window->flags & WindowFlags_NoSavedSettings) continue;
        if (!first) out << ",\n";
        first = false;
        out << "    {\"name\": \"" << JSONEscape(window->name) << "\", \"x\": " << window->pos.x
            << ", \"y\": " << window->pos.y << ", \"width\": " << window->size_full.x
            << ", \"height\": " << window->size_full.y << ", \"scrollY\": " << window->scroll_y
            << ", \"dock\": " << (int)window->dock_slot << ", \"collapsed\": "
            << (window->collapsed ? "true" : "false") << "}";
    }
    out << "\n  ]\n}\n";
    GOG->last_error.clear();
    return out.str();
}
bool SaveStateJSON(const char* path) {
    if (!GOG) return false;
    if (!path || !*path) { GOG->last_error = "Settings path is empty"; return false; }
    std::string json = SaveStateToMemory();
    FILE* file = OpenFilePortable(path, "wb");
    if (!file) { GOG->last_error = "Could not open settings file for writing"; return false; }
    size_t written = fwrite(json.data(), 1, json.size(), file);
    bool close_ok = fclose(file) == 0;
    if (written != json.size() || !close_ok) { GOG->last_error = "Could not write complete settings file"; return false; }
    GOG->last_error.clear();
    return true;
}
static bool JSONNumber(const std::string& source, const char* key, float& value,
                       size_t start = 0, size_t limit = std::string::npos) {
    if (limit == std::string::npos || limit > source.size()) limit = source.size();
    bool duplicate = false;
    size_t colon = FindJSONMemberColon(source, key, start, limit, &duplicate);
    if (colon == std::string::npos || duplicate) return false;
    size_t position = SkipJSONWhitespace(source, colon + 1, limit);
    size_t number_start = position;
    if (position < limit && source[position] == '-') ++position;
    if (position >= limit) return false;
    if (source[position] == '0') {
        ++position;
        if (position < limit && source[position] >= '0' && source[position] <= '9') return false;
    } else {
        if (source[position] < '1' || source[position] > '9') return false;
        while (position < limit && source[position] >= '0' && source[position] <= '9') ++position;
    }
    if (position < limit && source[position] == '.') {
        ++position;
        if (position >= limit || source[position] < '0' || source[position] > '9') return false;
        while (position < limit && source[position] >= '0' && source[position] <= '9') ++position;
    }
    if (position < limit && (source[position] == 'e' || source[position] == 'E')) {
        ++position;
        if (position < limit && (source[position] == '+' || source[position] == '-')) ++position;
        if (position >= limit || source[position] < '0' || source[position] > '9') return false;
        while (position < limit && source[position] >= '0' && source[position] <= '9') ++position;
    }
    if (!IsJSONMemberValueTerminated(source, position, limit)) return false;
    std::istringstream number_stream(source.substr(number_start, position - number_start));
    number_stream.imbue(std::locale::classic());
    double parsed = 0.0;
    number_stream >> parsed;
    if (!number_stream || !(parsed == parsed) || parsed > FLT_MAX || parsed < -FLT_MAX) return false;
    value = (float)parsed;
    return true;
}
static bool JSONBoolean(const std::string& source, const char* key, bool& value,
                         size_t start = 0, size_t limit = std::string::npos) {
    if (limit == std::string::npos || limit > source.size()) limit = source.size();
    bool duplicate = false;
    size_t colon = FindJSONMemberColon(source, key, start, limit, &duplicate);
    if (colon == std::string::npos || duplicate) return false;
    size_t position = SkipJSONWhitespace(source, colon + 1, limit);
    size_t end_position = position;
    if (position + 4 <= limit && source.compare(position, 4, "true") == 0) { value = true; end_position=position+4; }
    else if (position + 5 <= limit && source.compare(position, 5, "false") == 0) { value = false; end_position=position+5; }
    else return false;
    return IsJSONMemberValueTerminated(source, end_position, limit);
}
static int JSONOptionalNumber(const std::string& source, const char* key, float& value,
                               size_t start = 0, size_t limit = std::string::npos) {
    if (FindJSONMemberColon(source, key, start, limit) == std::string::npos) return 0;
    return JSONNumber(source, key, value, start, limit) ? 1 : -1;
}
static int JSONOptionalBoolean(const std::string& source, const char* key, bool& value,
                                size_t start = 0, size_t limit = std::string::npos) {
    if (FindJSONMemberColon(source, key, start, limit) == std::string::npos) return 0;
    return JSONBoolean(source, key, value, start, limit) ? 1 : -1;
}
static int JSONOptionalString(const std::string& source, const char* key, std::string& value,
                              size_t start = 0, size_t limit = std::string::npos) {
    if (limit == std::string::npos || limit > source.size()) limit = source.size();
    bool duplicate = false;
    size_t colon = FindJSONMemberColon(source, key, start, limit, &duplicate);
    if (colon == std::string::npos) return 0;
    if (duplicate) return -1;
    size_t quote_start = SkipJSONWhitespace(source, colon + 1, limit);
    if (quote_start >= limit || source[quote_start] != '"') return -1;
    size_t quote_end = FindJSONStringEnd(source, quote_start, limit);
    if (quote_end == std::string::npos ||
        !IsJSONMemberValueTerminated(source, quote_end + 1, limit)) return -1;
    return JSONUnescape(source.substr(quote_start + 1, quote_end - quote_start - 1), value) ? 1 : -1;
}
struct WindowLoadSnapshot {
    Vec2 pos;
    Vec2 size_full;
    float scroll_y;
    DockSlot dock_slot;
    bool collapsed;
    WindowLoadSnapshot(Window* window)
        : pos(window->pos), size_full(window->size_full), scroll_y(window->scroll_y),
          dock_slot(window->dock_slot), collapsed(window->collapsed) {}
};
struct StateLoadTransaction {
    Context* context;
    Style style;
    Style theme_from;
    Style theme_target;
    float ui_scale;
    ThemePreset theme_preset;
    bool reduced_motion;
    float theme_elapsed;
    float theme_duration;
    bool theme_transitioning;
    int focus_counter;
    size_t window_count;
    std::vector<WindowLoadSnapshot> windows;
    bool committed;
    explicit StateLoadTransaction(Context* value)
        : context(value), style(value->style), theme_from(value->theme_from), theme_target(value->theme_target),
          ui_scale(value->ui_scale), theme_preset(value->theme_preset),
          reduced_motion(value->io.config_reduced_motion), theme_elapsed(value->theme_elapsed),
          theme_duration(value->theme_duration), theme_transitioning(value->theme_transitioning),
          focus_counter(value->focus_counter), window_count(value->windows.size()), committed(false) {
        windows.reserve(window_count);
        for (size_t i = 0; i < window_count; ++i) windows.push_back(WindowLoadSnapshot(value->windows[i]));
    }
    ~StateLoadTransaction() {
        if (committed) return;
        while (context->windows.size() > window_count) {
            delete context->windows.back();
            context->windows.pop_back();
        }
        for (size_t i = 0; i < context->windows.size() && i < windows.size(); ++i) {
            context->windows[i]->pos = windows[i].pos;
            context->windows[i]->size_full = windows[i].size_full;
            context->windows[i]->scroll_y = windows[i].scroll_y;
            context->windows[i]->dock_slot = windows[i].dock_slot;
            context->windows[i]->collapsed = windows[i].collapsed;
        }
        context->style = style;
        context->theme_from = theme_from;
        context->theme_target = theme_target;
        context->ui_scale = ui_scale;
        context->theme_preset = theme_preset;
        context->io.config_reduced_motion = reduced_motion;
        context->theme_elapsed = theme_elapsed;
        context->theme_duration = theme_duration;
        context->theme_transitioning = theme_transitioning;
        context->focus_counter = focus_counter;
    }
    void Commit() { committed = true; }
private:
    StateLoadTransaction(const StateLoadTransaction&);
    StateLoadTransaction& operator=(const StateLoadTransaction&);
};
bool LoadStateFromMemory(const char* json, size_t length) {
    if (!GOG) return false;
    if (!json) { GOG->last_error = "Settings input is null"; return false; }
    if (length == 0) length = strlen(json);
    if (length == 0 || length > 16u * 1024u * 1024u) {
        GOG->last_error = "Settings input is empty or too large"; return false;
    }
    std::string source(json, length);
    size_t first = source.find_first_not_of(" \t\r\n");
    size_t last = source.find_last_not_of(" \t\r\n");
    if (first == std::string::npos || source[first] != '{' || source[last] != '}' ||
        FindJSONObjectEnd(source, first, source.size()) != last) {
        GOG->last_error = "Settings JSON has invalid top-level structure"; return false;
    }
    const size_t document_limit = last + 1;
    float schema = 0.0f;
    if (!JSONNumber(source, "version", schema, first, document_limit) ||
        (schema != 1.0f && schema != 2.0f)) {
        GOG->last_error = "Settings JSON has no supported schema"; return false;
    }
    StateLoadTransaction transaction(GOG);

    float value = 0.0f;
    int optional_state = JSONOptionalNumber(source, "uiScale", value, first, document_limit);
    if (optional_state < 0) { GOG->last_error = "Settings uiScale is malformed"; return false; }
    if (optional_state > 0) SetUIScale(value);
    optional_state = JSONOptionalNumber(source, "themePreset", value, first, document_limit);
    if (optional_state < 0) { GOG->last_error = "Settings themePreset is malformed"; return false; }
    if (optional_state > 0) {
        int preset = (int)value;
        if (value != (float)preset || preset < Theme_Dark || preset > Theme_HighContrast) {
            GOG->last_error = "Settings themePreset is out of range"; return false;
        }
        GOG->theme_preset = (ThemePreset)preset;
    }
    bool reduced_motion = false;
    optional_state = JSONOptionalBoolean(source, "reducedMotion", reduced_motion, first, document_limit);
    if (optional_state < 0) { GOG->last_error = "Settings reducedMotion is malformed"; return false; }
    if (optional_state > 0) GOG->io.config_reduced_motion = reduced_motion;

    size_t theme_start = 0, theme_limit = 0;
    int theme_state = JSONOptionalContainer(source, "theme", '{', theme_start, theme_limit,
                                            first, document_limit);
    if (theme_state < 0) { GOG->last_error = "Settings theme object is malformed"; return false; }

    Style& style = GOG->style;
    struct StyleField { const char* key; float* target; float minimum; float maximum; };
    StyleField style_fields[] = {
        {"windowPaddingX",&style.window_padding.x,0,200},{"windowPaddingY",&style.window_padding.y,0,200},
        {"framePaddingX",&style.frame_padding.x,0,100},{"framePaddingY",&style.frame_padding.y,0,100},
        {"itemSpacingX",&style.item_spacing.x,0,100},{"itemSpacingY",&style.item_spacing.y,0,100},
        {"itemInnerSpacingX",&style.item_inner_spacing.x,0,100},{"itemInnerSpacingY",&style.item_inner_spacing.y,0,100},
        {"indentSpacing",&style.indent_spacing,0,200},{"scrollbarSize",&style.scrollbar_size,4,100},
        {"grabMinSize",&style.grab_min_size,2,100},{"windowRounding",&style.window_rounding,0,100},
        {"frameRounding",&style.frame_rounding,0,100},{"shadowSize",&style.shadow_size,0,200},
        {"animationSpeed",&style.animation_speed,0.1f,100},{"disabledAlpha",&style.disabled_alpha,0,1},
        {"motionScale",&style.motion_scale,0,4}
    };
    if (theme_state > 0) {
        for (size_t field_index = 0; field_index < sizeof(style_fields) / sizeof(style_fields[0]); ++field_index) {
            optional_state = JSONOptionalNumber(source, style_fields[field_index].key, value,
                                                theme_start, theme_limit);
            if (optional_state < 0) {
                GOG->last_error = std::string("Settings theme field is malformed: ") + style_fields[field_index].key;
                return false;
            }
            if (optional_state > 0)
                *style_fields[field_index].target = Clamp(value, style_fields[field_index].minimum, style_fields[field_index].maximum);
        }
    }

    size_t array_start = 0, array_limit = 0;
    int colors_state = theme_state > 0
        ? JSONOptionalContainer(source, "colors", '[', array_start, array_limit, theme_start, theme_limit)
        : 0;
    if (colors_state < 0) { GOG->last_error = "Settings theme color array is malformed"; return false; }
    if (colors_state > 0) {
        size_t array_end = array_limit - 1;
        size_t position = array_start + 1; int color_count = 0;
        while (position < array_end) {
            position = SkipJSONWhitespace(source, position, array_end);
            if (position >= array_end) break;
            if (color_count >= Col_COUNT || source[position] < '0' || source[position] > '9') {
                GOG->last_error = "Settings theme color array contains an invalid value"; return false;
            }
            if (source[position] == '0' && position + 1 < array_end &&
                source[position + 1] >= '0' && source[position + 1] <= '9') {
                GOG->last_error = "Settings theme color array contains an invalid value"; return false;
            }
            char* end = 0;
            unsigned long parsed = strtoul(source.c_str() + position, &end, 10);
            size_t parsed_end = (size_t)(end - source.c_str());
            if (end == source.c_str() + position || parsed_end > array_end || parsed > 0xFFFFFFFFul) {
                GOG->last_error = "Settings theme color value is out of range"; return false;
            }
            style.colors[color_count++] = (U32)parsed;
            position = parsed_end;
            position = SkipJSONWhitespace(source, position, array_end);
            if (position < array_end) {
                if (source[position] != ',') { GOG->last_error = "Settings theme color array has an invalid separator"; return false; }
                ++position;
                size_t next = SkipJSONWhitespace(source, position, array_end);
                if (next >= array_end) { GOG->last_error = "Settings theme color array has a trailing comma"; return false; }
            }
        }
        if (color_count == 0) { GOG->last_error = "Settings theme color array is empty"; return false; }
    }

    int windows_state = JSONOptionalContainer(source, "windows", '[', array_start, array_limit,
                                              first, document_limit);
    if (windows_state < 0) { GOG->last_error = "Settings window array is malformed"; return false; }
    if (windows_state > 0) {
        size_t array_end = array_limit - 1;
        size_t position = array_start + 1;
        int window_count = 0;
        while (position < array_end) {
            position = SkipJSONWhitespace(source, position, array_end);
            if (position >= array_end) break;
            if (source[position] != '{') {
                GOG->last_error = "Settings window array contains a non-object entry"; return false;
            }
            size_t object_start = position;
            size_t object_end = FindJSONObjectEnd(source, object_start, array_end);
            if (object_end == std::string::npos) { GOG->last_error = "Settings window entry is malformed"; return false; }
            if (++window_count > 4096) { GOG->last_error = "Settings contains too many windows"; return false; }
            size_t object_limit = object_end + 1;
            std::string name;
            int name_state = JSONOptionalString(source, "name", name, object_start, object_limit);
            if (name_state <= 0) { GOG->last_error = "Settings window name is missing or malformed"; return false; }
            if (name.empty() || name.size() > 1024 || name.find('\0') != std::string::npos) {
                GOG->last_error = "Settings window name is invalid"; return false;
            }
            Window* window = FindWindow(name.c_str());
            if (!window) window = CreateWindowObj(name.c_str());
            float x = window->pos.x, y = window->pos.y, width = window->size_full.x, height = window->size_full.y;
            float dock = (float)window->dock_slot, scroll = window->scroll_y;
            const char* window_keys[] = {"x","y","width","height","scrollY","dock"};
            float* window_values[] = {&x,&y,&width,&height,&scroll,&dock};
            for (size_t field_index=0;field_index<sizeof(window_keys)/sizeof(window_keys[0]);++field_index) {
                int state=JSONOptionalNumber(source,window_keys[field_index],*window_values[field_index],object_start,object_limit);
                if(state<0){GOG->last_error=std::string("Settings window field is malformed: ")+window_keys[field_index];return false;}
            }
            window->pos = Vec2(Clamp(x, -100000, 100000), Clamp(y, -100000, 100000));
            window->size_full = Vec2(Clamp(width, 120, 100000), Clamp(height, 40, 100000));
            window->scroll_y = Max(scroll, 0);
            int dock_value = (int)dock;
            if (dock != (float)dock_value || dock_value < Dock_None || dock_value > Dock_Fill) {
                GOG->last_error="Settings window dock value is out of range";return false;
            }
            window->dock_slot = (DockSlot)dock_value;
            bool collapsed = window->collapsed;
            int collapsed_state=JSONOptionalBoolean(source,"collapsed",collapsed,object_start,object_limit);
            if(collapsed_state<0){GOG->last_error="Settings window collapsed value is malformed";return false;}
            if(collapsed_state>0)window->collapsed = collapsed;
            position = object_end + 1;
            position = SkipJSONWhitespace(source, position, array_end);
            if (position < array_end) {
                if (source[position] != ',') {
                    GOG->last_error = "Settings window array has an invalid separator"; return false;
                }
                position = SkipJSONWhitespace(source, position + 1, array_end);
                if (position >= array_end) {
                    GOG->last_error = "Settings window array has a trailing comma"; return false;
                }
            }
        }
    }
    GOG->theme_from = GOG->theme_target = GOG->style;
    transaction.Commit();
    GOG->last_error.clear();
    return true;
}
bool LoadStateJSON(const char* path) {
    if (!GOG) return false;
    if (!path || !*path) { GOG->last_error = "Settings path is empty"; return false; }
    FILE* file = OpenFilePortable(path, "rb");
    if (!file) { GOG->last_error = "Could not open settings file"; return false; }
    if (fseek(file, 0, SEEK_END) != 0) { fclose(file); GOG->last_error = "Could not inspect settings file"; return false; }
    long size = ftell(file);
    if (size <= 0 || size > 16 * 1024 * 1024) {
        fclose(file); GOG->last_error = "Settings file is empty or too large"; return false;
    }
    if (fseek(file, 0, SEEK_SET) != 0) { fclose(file); GOG->last_error = "Could not rewind settings file"; return false; }
    std::string source((size_t)size, '\0');
    size_t read = fread(&source[0], 1, (size_t)size, file);
    bool close_ok = fclose(file) == 0;
    if (read != (size_t)size || !close_ok) { GOG->last_error = "Could not read complete settings file"; return false; }
    return LoadStateFromMemory(source.data(), source.size());
}
const char* GetLastError(){return GOG?GOG->last_error.c_str():"No current OSGui context";}

void SetDebugLogCallback(DebugLogCallback callback,void* user_data){if(GOG){GOG->debug_log_callback=callback;GOG->debug_log_user_data=user_data;}}
bool ValidateState(){
    if(!GOG)return false;std::string problem;
    if(GOG->cur_window)problem="A window or popup scope is still open";
    else if(!GOG->window_parent_stack.empty())problem="Begin/End window stack is unbalanced";
    else if(!GridStack().empty())problem="BeginGrid/EndGrid stack is unbalanced";
    else if(!ChildStack().empty())problem="BeginChild/EndChild stack is unbalanced";
    else if(!Tables().empty())problem="BeginTable/EndTable stack is unbalanced";
    else if(!TabBars().empty())problem="BeginTabBar/EndTabBar stack is unbalanced";
    else if(!PopupParents().empty())problem="BeginPopup/EndPopup stack is unbalanced";
    else if(Chart().active)problem="BeginChart/EndChart stack is unbalanced";
    else if(NodeEditor().active)problem="BeginNodeEditor/EndNodeEditor stack is unbalanced";
    else if(!GOG->style_color_stack.empty())problem="PushStyleColor/PopStyleColor stack is unbalanced";
    else if(!GOG->style_var_stack.empty())problem="PushStyleVar/PopStyleVar stack is unbalanced";
    else if(!GOG->disabled_stack.empty())problem="BeginDisabled/EndDisabled stack is unbalanced";
    else if(!GOG->item_width_stack.empty())problem="PushItemWidth/PopItemWidth stack is unbalanced";
    else if(!GOG->tree_scope_stack.empty())problem="TreeNode/TreePop stack is unbalanced";
    if(problem.empty())for(size_t i=0;i<GOG->windows.size();++i){Window* w=GOG->windows[i];if(w->id_stack.size()>1){problem="PushID/PopID or composite ID scope is unbalanced";break;}if(!w->draw.effect_stack.empty()){problem="PushEffect/PopEffect stack is unbalanced";break;}if(!w->draw.texture_stack.empty()){problem="PushTexture/PopTexture stack is unbalanced";break;}}
    if(problem.empty()&&!GOG->overlay_draw.effect_stack.empty())problem="Overlay PushEffect/PopEffect stack is unbalanced";
    if(problem.empty()&&!GOG->overlay_draw.texture_stack.empty())problem="Overlay PushTexture/PopTexture stack is unbalanced";
    if(problem.empty()){GOG->last_error.clear();return true;}GOG->last_error=problem;if(GOG->debug_log_callback)GOG->debug_log_callback(problem.c_str(),GOG->debug_log_user_data);return false;
}
void ShowMetricsWindow(bool* p_open){
    if(p_open&&!*p_open)return;SetNextWindowSize(Vec2(430,430),Cond_FirstUseEver);
    if(!Begin("OSGui / Metrics Studio",p_open)){End();return;}const FrameMetrics& m=GOG->metrics_prev;
    TextColored(Vec4(.55f,.93f,.85f,1),"FRAME INSPECTOR");SameLine();StatusBadge(m.id_conflicts?"ID conflicts":"IDs clean",m.id_conflicts?GetColorU32(Col_Error):GetColorU32(Col_Success),m.id_conflicts!=0);
    Separator();if(BeginGrid("metrics",2,10)){char value[64];snprintf(value,sizeof(value),"%d",m.vertices);MetricCard("Vertices",value,"renderer-neutral");NextGridColumn();snprintf(value,sizeof(value),"%d",m.draw_commands);MetricCard("Draw commands",value,"after compaction",GetColorU32(Col_Info));NextGridColumn();snprintf(value,sizeof(value),"%d",m.items_submitted);MetricCard("Items",value,"submitted this frame",GetColorU32(Col_Success));NextGridColumn();snprintf(value,sizeof(value),"%d",m.clipped_items);MetricCard("Clipped",value,"culled by layout",GetColorU32(Col_Warning));EndGrid();}
    Text("Windows: %d  Lists: %d  Indices: %d",m.active_windows,m.draw_lists,m.indices);Text("Input events: %d  UI events: %d",m.input_events,m.ui_events);Text("Animation channels: %d",m.animation_states);
    if(m.id_conflicts>0){PushStyleColor(Col_ChildBg,ColorWithAlpha(GetColorU32(Col_Error),35));MetricCard("Identity warning","Duplicate IDs detected","Use PushID() inside repeated rows",GetColorU32(Col_Error));PopStyleColor();}
    TextDisabled("v%s / context %p",GetVersion(),(void*)GOG);End();
}

static void CleanupContextState(Context* context){
    GGridStacks.erase(context);GNextDockSlots.erase(context);GNextDockSets.erase(context);
    GChildStacks.erase(context);GChildScrollStates.erase(context);
    GTabBarsByContext.erase(context);GTabSelectionsByContext.erase(context);GTabLabelsByContext.erase(context);
    GToastsByContext.erase(context);GPopupOpenByContext.erase(context);GPopupParentsByContext.erase(context);GPopupIDsByContext.erase(context);
    GTablesByContext.erase(context);GTableSortsByContext.erase(context);GCharts.erase(context);GMarkdownConfigs.erase(context);GNodeEditors.erase(context);
}

} // namespace og
