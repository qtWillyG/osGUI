// OSGui - a compact, modern immediate-mode GUI for native C++ tools.
// The portable core emits draw data; platform and renderer backends handle the OS.

#pragma once
#include <stdint.h>
#include <vector>
#include <string>
#include <map>

namespace og {

// ------------------------------------------------------------------ math --
struct Vec2 {
    float x, y;
    Vec2() : x(0), y(0) {}
    Vec2(float _x, float _y) : x(_x), y(_y) {}
};
struct Vec4 {
    float x, y, z, w;
    Vec4() : x(0), y(0), z(0), w(0) {}
    Vec4(float _x, float _y, float _z, float _w) : x(_x), y(_y), z(_z), w(_w) {}
};

typedef uint32_t U32;
typedef uint32_t DrawIdx;
typedef uint64_t ID;

// color packed as R,G,B,A bytes (R in lowest byte) == GL RGBA memory order.
#define OG_COL32(R,G,B,A) (((og::U32)(A)<<24)|((og::U32)(B)<<16)|((og::U32)(G)<<8)|((og::U32)(R)))
#define OG_COL32_WHITE     OG_COL32(255,255,255,255)

// ------------------------------------------------------------ draw data --
struct DrawVert {
    Vec2 pos;
    Vec2 uv;
    U32  col;
};
struct DrawCmd {
    Vec4         clip_rect;   // x0,y0,x1,y1 in framebuffer space
    unsigned int tex_id;
    unsigned int idx_offset;  // first index
    unsigned int elem_count;  // number of indices
};

struct DrawList {
    std::vector<DrawVert> vtx;
    std::vector<DrawIdx>  idx;
    std::vector<DrawCmd>  cmds;
    std::vector<Vec4>     clip_stack;
    unsigned int          cur_tex;
    Vec2                  white_uv;

    void Clear();
    void PushClipRect(const Vec4& r);
    void PopClipRect();
    DrawCmd& CurCmd();
    void PrimReserve(int idx_count, int vtx_count);

    void AddRectFilled(const Vec2& a, const Vec2& b, U32 col);
    void AddRectFilledRounded(const Vec2& a, const Vec2& b, U32 col, float radius = 6.0f);
    void AddRectFilledMultiColor(const Vec2& a, const Vec2& b,
                                 U32 col_tl, U32 col_tr, U32 col_br, U32 col_bl);
    void AddShadowRect(const Vec2& a, const Vec2& b, U32 col,
                       float radius = 8.0f, float spread = 10.0f);
    void AddRect(const Vec2& a, const Vec2& b, U32 col, float thickness = 1.0f);
    void AddLine(const Vec2& a, const Vec2& b, U32 col, float thickness = 1.0f);
    void AddTriangleFilled(const Vec2& a, const Vec2& b, const Vec2& c, U32 col);
    void AddQuad(const Vec2& a, const Vec2& b, const Vec2& c, const Vec2& d, U32 col);
    void AddCircleFilled(const Vec2& c, float r, U32 col, int segs = 12);
    void AddText(const Vec2& pos, U32 col, const char* text, const char* text_end = 0);
    void AddGlyph(float x0, float y0, float x1, float y1,
                  float u0, float v0, float u1, float v1, U32 col);
};

struct DrawData {
    std::vector<DrawList*> lists;
    Vec2 display_pos;
    Vec2 display_size;
    int  total_vtx, total_idx;
};

// --------------------------------------------------------------- fonts ----
struct Glyph {
    float advance;
    float u0, v0, u1, v1;   // atlas texture coordinates
    float x0, y0, x1, y1;   // offset from pen position
};
struct FontAtlas {
    unsigned char* pixels;  // R8 coverage, owned by backend that bakes it
    int   width, height;
    float line_height;
    float ascent;
    Glyph glyphs[128];
    bool  glyph_valid[128];
    unsigned int tex_id;    // set by the renderer backend
    Vec2  white_uv;         // uv of a fully-opaque texel
};

// --------------------------------------------------------------- input ----
struct IO {
    Vec2  display_size;
    float delta_time;
    Vec2  mouse_pos;
    bool  mouse_down[3];
    float mouse_wheel;
    // text + keys (filled by the platform backend)
    unsigned int input_chars[32];
    int          input_char_count;
    bool         key_down[256];
    // computed
    float framerate;
    bool  want_capture_mouse;
};

// -------------------------------------------------------------- style -----
enum Col_ {
    Col_Text, Col_TextDisabled, Col_WindowBg, Col_TitleBg, Col_TitleBgActive,
    Col_MenuBarBg, Col_Border, Col_FrameBg, Col_FrameBgHovered, Col_FrameBgActive,
    Col_Button, Col_ButtonHovered, Col_ButtonActive,
    Col_Header, Col_HeaderHovered, Col_HeaderActive,
    Col_CheckMark, Col_SliderGrab, Col_SliderGrabActive, Col_Separator,
    Col_ResizeGrip, Col_ResizeGripHovered, Col_ResizeGripActive,
    Col_ScrollbarBg, Col_ScrollbarGrab, Col_PlotLines, Col_PlotHistogram,
    Col_WindowShadow, Col_GradientStart, Col_GradientEnd, Col_CodeBg,
    Col_Link, Col_Success, Col_Warning,
    Col_COUNT
};
struct Style {
    Vec2  window_padding;
    Vec2  frame_padding;
    Vec2  item_spacing;
    Vec2  item_inner_spacing;
    float indent_spacing;
    float scrollbar_size;
    float grab_min_size;
    float window_title_height;   // computed in NewFrame
    float window_rounding;
    float frame_rounding;
    float shadow_size;
    float animation_speed;
    U32   colors[Col_COUNT];
    Style();
};

enum ThemePreset {
    Theme_Dark,
    Theme_Light
};

enum EventType {
    Event_Clicked,
    Event_ValueChanged,
    Event_WindowClosed
};

struct Event {
    EventType   type;
    ID          id;
    std::string label;
    Event() : type(Event_Clicked), id(0) {}
    Event(EventType t, ID i, const char* l) : type(t), id(i), label(l ? l : "") {}
};

struct AnimationState {
    float value;
    float target;
    float velocity;
    int   last_frame;
    AnimationState() : value(0), target(0), velocity(0), last_frame(0) {}
};

class StreamingSeries {
public:
    explicit StreamingSeries(int capacity = 512);
    void Push(float value);
    void Clear();
    int  Size() const;
    int  Capacity() const;
    void GetOrdered(std::vector<float>& out) const;

private:
    std::vector<float> values_;
    int head_;
    int count_;
};

// ------------------------------------------------------------- context ----
struct Window;

struct Context {
    IO        io;
    Style     style;
    FontAtlas atlas;
    DrawData  draw_data;

    std::vector<Window*> windows;
    Window*   cur_window;
    Window*   hovered_window;
    Window*   moving_window;
    Window*   nav_window;          // focused window

    ID        active_id;
    Window*   active_id_window;
    ID        hovered_id;
    Vec2      active_id_click_offset;

    bool      mouse_down_prev[3];
    bool      mouse_clicked[3];
    bool      mouse_released[3];
    Vec2      mouse_pos_prev;

    int       frame_count;
    int       focus_counter;
    double    time;
    float     framerate_acc;

    std::map<ID, int> storage;     // open/closed state for headers & tree nodes
    std::map<ID, AnimationState> animations;
    std::vector<Event> events;

    Style theme_from;
    Style theme_target;
    float theme_elapsed;
    float theme_duration;
    bool  theme_transitioning;
    ThemePreset theme_preset;

    bool next_pos_set, next_size_set;
    Vec2 next_pos, next_size;

    Context();
};

// ------------------------------------------------------------- public API -
Context*    CreateContext();
void        DestroyContext();
Context&    GetContext();
IO&         GetIO();
Style&      GetStyle();
FontAtlas&  GetFontAtlas();
DrawData*   GetDrawData();
const std::vector<Event>& GetEvents();

void NewFrame();
void Render();

// themes and animation
Style GetBuiltinTheme(ThemePreset preset);
void  SetTheme(ThemePreset preset, float transition_seconds = 0.25f);
bool  IsThemeTransitioning();
float Animate(const char* key, float target, float speed = 0.0f);

// windows
bool Begin(const char* name, bool* p_open = 0);
void End();
void SetNextWindowPos(const Vec2& pos);    // applied once, on first appearance
void SetNextWindowSize(const Vec2& size);  // applied once, on first appearance

// layout
void SameLine(float offset_x = 0.0f, float spacing = -1.0f);
void Spacing();
void Separator();
void Indent(float w = 0.0f);
void Unindent(float w = 0.0f);
Vec2 GetContentRegionAvail();
bool BeginGrid(const char* id, int columns, float gap = 12.0f);
void NextGridColumn();
void EndGrid();

// widgets
void Text(const char* fmt, ...);
void TextDisabled(const char* fmt, ...);
void TextColored(const Vec4& col, const char* fmt, ...);
void BulletText(const char* fmt, ...);
bool Button(const char* label, const Vec2& size = Vec2(0, 0));
bool SmallButton(const char* label);
bool Checkbox(const char* label, bool* v);
bool RadioButton(const char* label, int* v, int v_button);
bool SliderFloat(const char* label, float* v, float v_min, float v_max, const char* fmt = "%.3f");
bool SliderInt(const char* label, int* v, int v_min, int v_max);
bool CollapsingHeader(const char* label);
bool TreeNode(const char* label);
void TreePop();
void ProgressBar(float fraction, const Vec2& size = Vec2(-1, 0), const char* overlay = 0);
void PlotLines(const char* label, const float* values, int count, const Vec2& size = Vec2(-1, 60));
void PlotHistogram(const char* label, const float* values, int count, const Vec2& size = Vec2(-1, 60));

// chart builder: collect one or more series, then render together in EndChart
bool BeginChart(const char* label, const Vec2& size = Vec2(-1, 150));
void ChartLine(const char* label, const float* values, int count, U32 color = 0);
void ChartLine(const char* label, const StreamingSeries& series, U32 color = 0);
void ChartBars(const char* label, const float* values, int count, U32 color = 0);
void EndChart();

// lightweight built-in rich text / Markdown
void Markdown(const char* markdown);

// helpers
Vec2 CalcTextSize(const char* text, const char* text_end = 0);
U32  GetColorU32(int idx);

// demo (osgui_demo.cpp)
void ShowDemoWindow(bool* p_open = 0);

} // namespace og
