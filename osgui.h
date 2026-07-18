// OSGui v2 - immediate-mode UI for instrument-grade native C++ tools.
// The portable core emits draw data; platform and renderer backends handle the OS.

#pragma once
#include <stdint.h>
#include <stddef.h>
#include <vector>
#include <string>
#include <map>

#define OSGUI_VERSION       "2.0.0-alpha.1"
#define OSGUI_VERSION_NUM   20000
#define OSGUI_VERSION_MAJOR 2
#define OSGUI_VERSION_MINOR 0
#define OSGUI_VERSION_PATCH 0

#define OG_CHECKVERSION() \
    og::DebugCheckVersionAndDataLayout(OSGUI_VERSION, sizeof(og::IO), sizeof(og::Style), \
                                       sizeof(og::DrawVert), sizeof(og::DrawIdx))

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
typedef uintptr_t TextureID;

// color packed as R,G,B,A bytes (R in lowest byte) == GL RGBA memory order.
#define OG_COL32(R,G,B,A) (((og::U32)(A)<<24)|((og::U32)(B)<<16)|((og::U32)(G)<<8)|((og::U32)(R)))
#define OG_COL32_WHITE     OG_COL32(255,255,255,255)

// ------------------------------------------------------------ draw data --
struct DrawVert {
    Vec2 pos;
    Vec2 uv;
    U32  col;
};
enum DrawEffect_ {
    DrawEffect_None,
    DrawEffect_BackdropBlur
};
struct DrawList;
struct DrawCmd;
typedef void (*DrawCallback)(const DrawList* parent_list, const DrawCmd* command);
struct DrawCmd {
    Vec4         clip_rect;   // x0,y0,x1,y1 in logical display coordinates
    TextureID    tex_id;
    unsigned int idx_offset;  // first index
    unsigned int vtx_offset;  // base vertex for large meshes/backends
    unsigned int elem_count;  // number of indices
    int          effect;
    float        effect_amount;
    DrawCallback callback;
    void*        callback_data;

    DrawCmd();
};

struct DrawList {
    std::vector<DrawVert> vtx;
    std::vector<DrawIdx>  idx;
    std::vector<DrawCmd>  cmds;
    std::vector<Vec4>     clip_stack;
    std::vector<TextureID> texture_stack;
    std::vector<int>      effect_stack;
    std::vector<float>    effect_amount_stack;
    TextureID             cur_tex;
    int                   cur_effect;
    float                 cur_effect_amount;
    Vec2                  white_uv;

    DrawList();
    void Clear();
    void PushClipRect(const Vec4& r, bool intersect_with_current = true);
    void PopClipRect();
    DrawCmd& CurCmd();
    void PrimReserve(int idx_count, int vtx_count);
    void PushEffect(int effect, float amount = 0.0f);
    void PopEffect();
    void PushTexture(TextureID texture_id);
    void PopTexture();
    void AddCallback(DrawCallback callback, void* callback_data = 0);
    void CompactCommands();

    void AddRectFilled(const Vec2& a, const Vec2& b, U32 col);
    void AddRectFilledRounded(const Vec2& a, const Vec2& b, U32 col, float radius = 6.0f);
    void AddRectFilledMultiColor(const Vec2& a, const Vec2& b,
                                 U32 col_tl, U32 col_tr, U32 col_br, U32 col_bl);
    void AddShadowRect(const Vec2& a, const Vec2& b, U32 col,
                       float radius = 8.0f, float spread = 10.0f);
    void AddBackdropBlur(const Vec2& a, const Vec2& b, U32 tint,
                         float radius = 8.0f, float rounding = 8.0f);
    void AddRect(const Vec2& a, const Vec2& b, U32 col, float thickness = 1.0f);
    void AddLine(const Vec2& a, const Vec2& b, U32 col, float thickness = 1.0f);
    void AddTriangleFilled(const Vec2& a, const Vec2& b, const Vec2& c, U32 col);
    void AddQuad(const Vec2& a, const Vec2& b, const Vec2& c, const Vec2& d, U32 col);
    void AddCircleFilled(const Vec2& c, float r, U32 col, int segs = 12);
    void AddCircle(const Vec2& c, float r, U32 col, int segs = 24, float thickness = 1.0f);
    void AddBezierCubic(const Vec2& p1, const Vec2& p2, const Vec2& p3, const Vec2& p4,
                        U32 col, float thickness = 1.0f, int segments = 0);
    void AddText(const Vec2& pos, U32 col, const char* text, const char* text_end = 0);
    void AddGlyph(float x0, float y0, float x1, float y1,
                  float u0, float v0, float u1, float v1, U32 col);
};

struct DrawData {
    std::vector<DrawList*> lists;
    Vec2 display_pos;
    Vec2 display_size;
    int  total_vtx, total_idx;
    DrawData() : display_pos(), display_size(), total_vtx(0), total_idx(0) {}
};

// Owns a deep copy of renderer-neutral output for recording, testing, or remote transport.
struct DrawDataSnapshot {
    std::vector<DrawList> owned_lists;
    DrawData data;
    DrawDataSnapshot();
    DrawDataSnapshot(const DrawDataSnapshot& other);
    DrawDataSnapshot& operator=(const DrawDataSnapshot& other);
    void Capture(const DrawData* source);
    void Clear();
    const DrawData* GetDrawData() const { return &data; }
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
    std::map<unsigned int, Glyph> glyph_map; // UTF-8 decoded codepoint -> glyph
    TextureID tex_id;       // set by the renderer backend
    Vec2  white_uv;         // uv of a fully-opaque texel
    FontAtlas();
};

// --------------------------------------------------------------- input ----
// Values intentionally match the common Win32 virtual-key range for source
// compatibility. Non-Win32 backends translate native keys to this enum.
enum Key {
    Key_None = 0,
    Key_Backspace = 8, Key_Tab = 9, Key_Enter = 13, Key_Shift = 16,
    Key_Ctrl = 17, Key_Alt = 18, Key_Escape = 27, Key_Space = 32,
    Key_PageUp = 33, Key_PageDown = 34, Key_End = 35, Key_Home = 36,
    Key_LeftArrow = 37, Key_UpArrow = 38, Key_RightArrow = 39, Key_DownArrow = 40,
    Key_Insert = 45, Key_Delete = 46,
    Key_0 = '0', Key_1 = '1', Key_2 = '2', Key_3 = '3', Key_4 = '4',
    Key_5 = '5', Key_6 = '6', Key_7 = '7', Key_8 = '8', Key_9 = '9',
    Key_A = 'A', Key_B = 'B', Key_C = 'C', Key_D = 'D', Key_E = 'E',
    Key_F = 'F', Key_G = 'G', Key_H = 'H', Key_I = 'I', Key_J = 'J',
    Key_K = 'K', Key_L = 'L', Key_M = 'M', Key_N = 'N', Key_O = 'O',
    Key_P = 'P', Key_Q = 'Q', Key_R = 'R', Key_S = 'S', Key_T = 'T',
    Key_U = 'U', Key_V = 'V', Key_W = 'W', Key_X = 'X', Key_Y = 'Y', Key_Z = 'Z',
    Key_F1 = 112, Key_F2, Key_F3, Key_F4, Key_F5, Key_F6,
    Key_F7, Key_F8, Key_F9, Key_F10, Key_F11, Key_F12,
    Key_COUNT = 512
};

enum InputEventType {
    InputEvent_None,
    InputEvent_MousePos,
    InputEvent_MouseButton,
    InputEvent_MouseWheel,
    InputEvent_Key,
    InputEvent_Text,
    InputEvent_Focus
};

struct InputEvent {
    InputEventType type;
    int key_or_button;
    bool down;
    float x, y;
    unsigned int codepoint;
    InputEvent() : type(InputEvent_None), key_or_button(0), down(false), x(0), y(0), codepoint(0) {}
};

enum BackendFlags_ {
    BackendFlags_None                 = 0,
    BackendFlags_HasClipboard         = 1 << 0,
    BackendFlags_HasMouseCursors      = 1 << 1,
    BackendFlags_HasSetMousePos       = 1 << 2,
    BackendFlags_RendererHasTextures  = 1 << 3,
    BackendFlags_RendererHasEffects   = 1 << 4,
    BackendFlags_RendererHasVtxOffset = 1 << 5
};

struct IO {
    Vec2  display_size;
    Vec2  framebuffer_scale;
    float dpi_scale;
    float delta_time;
    Vec2  mouse_pos;
    bool  mouse_down[5];
    float mouse_wheel;
    float mouse_wheel_h;
    // text + keys (filled by the platform backend)
    std::vector<unsigned int> input_chars;
    bool         key_down[Key_COUNT];
    std::vector<InputEvent> input_events;
    const char* (*get_clipboard_text)(void* user_data);
    void (*set_clipboard_text)(void* user_data, const char* text);
    void* clipboard_user_data;
    const char* backend_platform_name;
    const char* backend_renderer_name;
    int   backend_flags;
    bool  app_focused;
    bool  config_reduced_motion;
    // computed
    float framerate;
    bool  want_capture_mouse;
    bool  want_capture_keyboard;
    bool  want_text_input;

    IO();
    void AddMousePosEvent(float x, float y);
    void AddMouseButtonEvent(int button, bool down);
    void AddMouseWheelEvent(float horizontal, float vertical);
    void AddKeyEvent(Key key, bool down);
    void AddInputCharacter(unsigned int codepoint);
    void AddFocusEvent(bool focused);
    void ClearInputEvents();
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
    Col_NodeBg, Col_NodeTitle, Col_NodeGrid, Col_NodeLink,
    Col_Info, Col_Error, Col_FocusRing, Col_Selection,
    Col_ChildBg, Col_DragDropTarget, Col_ModalDim,
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
    float disabled_alpha;
    float motion_scale;
    U32   colors[Col_COUNT];
    Style();
};

enum StyleVar {
    StyleVar_WindowPadding,
    StyleVar_FramePadding,
    StyleVar_ItemSpacing,
    StyleVar_ItemInnerSpacing,
    StyleVar_IndentSpacing,
    StyleVar_ScrollbarSize,
    StyleVar_GrabMinSize,
    StyleVar_WindowRounding,
    StyleVar_FrameRounding,
    StyleVar_ShadowSize,
    StyleVar_AnimationSpeed,
    StyleVar_DisabledAlpha,
    StyleVar_MotionScale
};

enum ThemePreset {
    Theme_Dark,
    Theme_Light,
    Theme_HighContrast
};

enum Cond_ {
    Cond_None         = 0,
    Cond_Always       = 1 << 0,
    Cond_Once         = 1 << 1,
    Cond_FirstUseEver = 1 << 2,
    Cond_Appearing    = 1 << 3
};

enum WindowFlags_ {
    WindowFlags_None                   = 0,
    WindowFlags_NoTitleBar             = 1 << 0,
    WindowFlags_NoResize               = 1 << 1,
    WindowFlags_NoMove                 = 1 << 2,
    WindowFlags_NoScrollbar            = 1 << 3,
    WindowFlags_NoCollapse             = 1 << 4,
    WindowFlags_AlwaysAutoResize       = 1 << 5,
    WindowFlags_NoBackground           = 1 << 6,
    WindowFlags_NoSavedSettings        = 1 << 7,
    WindowFlags_NoBringToFrontOnFocus  = 1 << 8
};

enum ChildFlags_ {
    ChildFlags_None       = 0,
    ChildFlags_Borders    = 1 << 0,
    ChildFlags_AlwaysUseWindowPadding = 1 << 1,
    ChildFlags_NoScrollbar = 1 << 2
};

enum DockSlot {
    Dock_None, Dock_Left, Dock_Right, Dock_Top, Dock_Bottom, Dock_Fill
};

enum EventType {
    Event_Clicked,
    Event_ValueChanged,
    Event_WindowClosed,
    Event_TextChanged,
    Event_LinkActivated,
    Event_DragDropDelivered
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

enum ItemStatusFlags_ {
    ItemStatus_None       = 0,
    ItemStatus_Hovered    = 1 << 0,
    ItemStatus_Active     = 1 << 1,
    ItemStatus_Focused    = 1 << 2,
    ItemStatus_Clicked    = 1 << 3,
    ItemStatus_Edited     = 1 << 4,
    ItemStatus_Disabled   = 1 << 5,
    ItemStatus_Visible    = 1 << 6
};

struct LastItemData {
    ID id;
    Vec4 rect;
    int status_flags;
    LastItemData() : id(0), rect(), status_flags(ItemStatus_None) {}
};

struct Payload {
    const void* data;
    int data_size;
    ID source_id;
    bool preview;
    bool delivery;
    Payload() : data(0), data_size(0), source_id(0), preview(false), delivery(false) {}
};

struct FrameMetrics {
    int frame_number;
    int active_windows;
    int items_submitted;
    int clipped_items;
    int id_conflicts;
    int draw_lists;
    int draw_commands;
    int vertices;
    int indices;
    int input_events;
    int ui_events;
    int animation_states;
    FrameMetrics();
};

typedef void (*DebugLogCallback)(const char* message, void* user_data);

struct StyleColorBackup { int index; U32 value; StyleColorBackup(int i=0, U32 v=0) : index(i), value(v) {} };
struct StyleVarBackup {
    StyleVar index;
    Vec2 vec_value;
    float float_value;
    bool is_vec2;
    StyleVarBackup() : index(StyleVar_WindowPadding), vec_value(), float_value(0), is_vec2(false) {}
};

struct NodePin {
    ID id;
    Vec2 position;
    NodePin() : id(0), position() {}
    NodePin(ID i, const Vec2& p) : id(i), position(p) {}
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
    DrawList  overlay_draw;

    std::vector<Window*> windows;
    std::vector<Window*> window_parent_stack;
    Window*   cur_window;
    Window*   hovered_window;
    Window*   moving_window;
    Window*   nav_window;          // focused window
    Window*   modal_window;        // top-level modal that blocks background input

    ID        active_id;
    ID        text_active_id;
    Window*   active_id_window;
    Window*   text_active_id_window;
    ID        hovered_id;
    Vec2      active_id_click_offset;

    bool      mouse_down_prev[5];
    bool      mouse_clicked[5];
    bool      mouse_released[5];
    Vec2      mouse_clicked_pos[5];
    Vec2      mouse_released_pos[5];
    int       mouse_clicked_mods[5];
    int       mouse_released_mods[5];
    bool      key_down_prev[Key_COUNT];
    bool      key_pressed[Key_COUNT];
    bool      key_consumed[Key_COUNT];
    int       key_pressed_mods[Key_COUNT];
    float     key_down_duration[Key_COUNT];
    Vec2      mouse_pos_prev;
    bool      focus_lost_this_frame;

    int       frame_count;
    int       focus_counter;
    double    time;
    float     framerate_acc;

    std::map<ID, int> storage;     // open/closed state for headers & tree nodes
    std::map<ID, AnimationState> animations;
    std::vector<Event> events;
    std::vector<ID> nav_order;
    std::vector<ID> nav_order_prev;
    std::vector<Window*> nav_order_windows;
    std::vector<Window*> nav_order_windows_prev;
    ID nav_id;
    ID nav_activate_id;
    Key nav_activate_key;
    std::map<ID, int> text_cursor;
    std::map<ID, int> text_selection_anchor;
    std::map<ID, Vec2> text_scroll;
    std::map<ID, std::vector<std::string> > text_undo;
    std::map<ID, std::vector<std::string> > text_redo;
    std::map<ID, double> drag_accumulator;
    std::map<ID, Vec4> submitted_ids;
    std::map<ID, int> markdown_call_counts;

    LastItemData last_item;
    FrameMetrics metrics;
    FrameMetrics metrics_prev;
    int disabled_depth;
    std::vector<bool> disabled_stack;
    std::vector<float> item_width_stack;
    float next_item_width;
    bool next_item_width_set;
    std::vector<StyleColorBackup> style_color_stack;
    std::vector<StyleVarBackup> style_var_stack;
    std::vector<ID> tree_scope_stack;

    std::string drag_payload_type;
    std::vector<unsigned char> drag_payload_data;
    Payload drag_payload;
    bool drag_drop_active;
    bool drag_drop_target;
    bool drag_drop_delivered;

    DebugLogCallback debug_log_callback;
    void* debug_log_user_data;
    std::string last_error;

    Style theme_from;
    Style theme_target;
    float theme_elapsed;
    float theme_duration;
    bool  theme_transitioning;
    ThemePreset theme_preset;
    float ui_scale;

    bool next_pos_set, next_size_set;
    Vec2 next_pos, next_size;
    int next_pos_cond, next_size_cond;
    bool next_constraints_set;
    Vec2 next_size_min, next_size_max;

    Context();
};

// ------------------------------------------------------------- public API -
const char* GetVersion();
bool        DebugCheckVersionAndDataLayout(const char* version, size_t io_size, size_t style_size,
                                            size_t draw_vert_size, size_t draw_idx_size);
Context*    CreateContext();
void        DestroyContext(Context* context = 0);
Context*    GetCurrentContext();
void        SetCurrentContext(Context* context);
Context&    GetContext();
IO&         GetIO();
Style&      GetStyle();
FontAtlas&  GetFontAtlas();
DrawData*   GetDrawData();
const std::vector<Event>& GetEvents();

void NewFrame();
void Render();

// input helpers (backends may use these instead of mutating IO arrays directly)
bool IsKeyDown(Key key);
bool IsKeyPressed(Key key, bool repeat = false);
bool IsKeyPressed(int legacy_key); // 0.x compatibility
bool IsMouseDown(int button);
bool IsMouseClicked(int button);
bool IsMouseReleased(int button);
Vec2 GetMousePos();
Vec2 GetMouseDragDelta(int button = 0, float lock_threshold = -1.0f);

// themes and animation
Style GetBuiltinTheme(ThemePreset preset);
void  SetTheme(ThemePreset preset, float transition_seconds = 0.25f);
bool  IsThemeTransitioning();
float Animate(const char* key, float target, float speed = 0.0f);
void  SetUIScale(float scale);
float GetUIScale();
void  SetReducedMotion(bool reduced);
bool  IsReducedMotion();

// stable identity, scoped presentation, and interaction scopes
ID    GetID(const char* label);
void  PushID(const char* value);
void  PushID(int value);
void  PushID(const void* value);
void  PopID();
void  PushStyleColor(int color_index, U32 color);
void  PopStyleColor(int count = 1);
void  PushStyleVar(StyleVar style_var, float value);
void  PushStyleVar(StyleVar style_var, const Vec2& value);
void  PopStyleVar(int count = 1);
void  BeginDisabled(bool disabled = true);
void  EndDisabled();
void  PushItemWidth(float item_width);
void  PopItemWidth();
void  SetNextItemWidth(float item_width);

// windows
bool Begin(const char* name, bool* p_open = 0, int flags = WindowFlags_None);
void End();
void SetNextWindowPos(const Vec2& pos, int cond = Cond_Once);
void SetNextWindowSize(const Vec2& size, int cond = Cond_Once);
void SetNextWindowSizeConstraints(const Vec2& min_size, const Vec2& max_size);
void SetNextWindowDock(DockSlot slot);
void DockWindow(const char* name, DockSlot slot);
Vec2 GetWindowPos();
Vec2 GetWindowSize();
float GetWindowScrollY();
float GetWindowScrollMaxY();
void SetWindowPos(const Vec2& pos);
void SetWindowSize(const Vec2& size);
void SetWindowScrollY(float scroll_y);
void SetWindowCollapsed(bool collapsed);

// layout
void SameLine(float offset_x = 0.0f, float spacing = -1.0f);
void Spacing();
void Separator();
void Indent(float w = 0.0f);
void Unindent(float w = 0.0f);
Vec2 GetContentRegionAvail();
Vec2 GetCursorScreenPos();
void SetCursorScreenPos(const Vec2& pos);
bool BeginGrid(const char* id, int columns, float gap = 12.0f);
void NextGridColumn();
void EndGrid();
bool BeginChild(const char* id, const Vec2& size = Vec2(0, 0), int child_flags = ChildFlags_Borders);
void EndChild();

class ListClipper {
public:
    int display_start;
    int display_end;
    ListClipper();
    ~ListClipper();
    void Begin(int item_count, float item_height = -1.0f);
    bool Step();
    void End();
private:
    int item_count_;
    float item_height_;
    float start_y_;
    bool stepped_;
};

// widgets
void TextUnformatted(const char* text, const char* text_end = 0);
void Text(const char* fmt, ...);
void TextDisabled(const char* fmt, ...);
void TextColored(const Vec4& col, const char* fmt, ...);
void TextWrapped(const char* fmt, ...);
void BulletText(const char* fmt, ...);
bool Button(const char* label, const Vec2& size = Vec2(0, 0));
bool SmallButton(const char* label);
bool InvisibleButton(const char* id, const Vec2& size);
bool Selectable(const char* label, bool selected = false, const Vec2& size = Vec2(0, 0));
bool Checkbox(const char* label, bool* v);
bool RadioButton(const char* label, int* v, int v_button);
bool SliderFloat(const char* label, float* v, float v_min, float v_max, const char* fmt = "%.3f");
bool SliderInt(const char* label, int* v, int v_min, int v_max);
bool DragFloat(const char* label, float* value, float speed = 1.0f,
               float min_value = 0.0f, float max_value = 0.0f, const char* format = "%.3f");
bool DragInt(const char* label, int* value, float speed = 1.0f,
             int min_value = 0, int max_value = 0);
bool KnobFloat(const char* label, float* value, float min_value, float max_value,
               float speed = 0.01f, const Vec2& size = Vec2(72, 88));
enum InputTextFlags_ {
    InputTextFlags_None = 0,
    InputTextFlags_Password = 1 << 0,
    InputTextFlags_ReadOnly = 1 << 1,
    InputTextFlags_EnterReturnsTrue = 1 << 2,
    InputTextFlags_AutoSelectAll = 1 << 3,
    InputTextFlags_CtrlEnterForNewLine = 1 << 4,
    InputTextFlags_EscapeClearsAll = 1 << 5,
    InputTextFlags_NoUndoRedo = 1 << 6
};
bool InputText(const char* label, char* buffer, int buffer_size, int flags = InputTextFlags_None);
bool InputTextMultiline(const char* label, char* buffer, int buffer_size,
                        const Vec2& size = Vec2(-1, 100), int flags = InputTextFlags_None);
bool Combo(const char* label, int* current_item, const char* const items[], int item_count);
bool BeginTabBar(const char* id);
bool BeginTabItem(const char* label, bool* p_open = 0);
void EndTabItem();
void EndTabBar();
bool ColorEdit4(const char* label, float color[4]);
bool CollapsingHeader(const char* label);
bool TreeNode(const char* label);
void TreePop();
void ProgressBar(float fraction, const Vec2& size = Vec2(-1, 0), const char* overlay = 0);
void GlassCard(const char* label, const Vec2& size = Vec2(-1, 72), float blur_radius = 8.0f);
void StatusBadge(const char* label, U32 color = 0, bool pulse = false);
void Spinner(const char* label, float radius = 9.0f, float thickness = 2.0f, U32 color = 0);
void Skeleton(const Vec2& size, float rounding = -1.0f);
void MetricCard(const char* label, const char* value, const char* detail = 0,
                U32 accent = 0, const Vec2& size = Vec2(-1, 82));
void Image(TextureID texture_id, const Vec2& size, const Vec2& uv0 = Vec2(0, 0), const Vec2& uv1 = Vec2(1, 1), U32 tint = OG_COL32_WHITE);
void SetTooltip(const char* text);
enum ToastType { Toast_Info, Toast_Success, Toast_Warning, Toast_Error };
void AddToast(const char* message, ToastType type = Toast_Info, float duration = 3.0f);
void RenderNotifications();
void OpenPopup(const char* id);
bool BeginPopup(const char* id);
bool BeginPopupModal(const char* title, bool* p_open = 0);
void CloseCurrentPopup();
void EndPopup();

// information about the widget submitted immediately before this call
bool IsItemHovered();
bool IsItemActive();
bool IsItemFocused();
bool IsItemClicked(int mouse_button = 0);
bool IsItemEdited();
bool IsItemVisible();
ID   GetItemID();
Vec2 GetItemRectMin();
Vec2 GetItemRectMax();
Vec2 GetItemRectSize();

// typed, copy-owned drag/drop payloads
bool BeginDragDropSource();
bool SetDragDropPayload(const char* type, const void* data, int data_size);
void EndDragDropSource();
bool BeginDragDropTarget();
const Payload* AcceptDragDropPayload(const char* type);
void EndDragDropTarget();

enum TableFlags_ { TableFlags_None = 0, TableFlags_RowBg = 1 << 0, TableFlags_Borders = 1 << 1, TableFlags_Sortable = 1 << 2 };
enum SortDirection { Sort_None, Sort_Ascending, Sort_Descending };
struct TableSortSpec { int column; SortDirection direction; bool dirty; TableSortSpec() : column(-1), direction(Sort_None), dirty(false) {} };
bool BeginTable(const char* id, int columns, int flags = TableFlags_RowBg | TableFlags_Borders);
bool TableHeader(const char* label);
void TableNextRow();
void TableNextColumn();
bool TableSelectable(const char* label, bool selected = false);
const TableSortSpec* TableGetSortSpec();
void EndTable();
void PlotLines(const char* label, const float* values, int count, const Vec2& size = Vec2(-1, 60));
void PlotHistogram(const char* label, const float* values, int count, const Vec2& size = Vec2(-1, 60));

// chart builder: collect one or more series, then render together in EndChart
bool BeginChart(const char* label, const Vec2& size = Vec2(-1, 150));
void ChartLine(const char* label, const float* values, int count, U32 color = 0);
void ChartLine(const char* label, const StreamingSeries& series, U32 color = 0);
void ChartBars(const char* label, const float* values, int count, U32 color = 0);
void ChartArea(const char* label, const float* values, int count, U32 color = 0);
void ChartScatter(const char* label, const Vec2* points, int count, U32 color = 0);
void ChartPie(const char* label, const float* values, const char* const labels[], int count);
struct Candlestick { float open, high, low, close; Candlestick(float o=0, float h=0, float l=0, float c=0) : open(o), high(h), low(l), close(c) {} };
void ChartCandlesticks(const char* label, const Candlestick* values, int count);
void EndChart();

// lightweight built-in rich text / Markdown
void Markdown(const char* markdown);
typedef void (*MarkdownLinkCallback)(const char* url, void* user_data);
typedef bool (*MarkdownImageResolver)(const char* url, TextureID* texture_id, Vec2* size, void* user_data);
void SetMarkdownLinkCallback(MarkdownLinkCallback callback, void* user_data = 0);
void SetMarkdownImageResolver(MarkdownImageResolver resolver, void* user_data = 0);

// JSON persistence for window layouts, docking, UI scale, and complete themes.
bool SaveStateJSON(const char* path);
bool LoadStateJSON(const char* path);
std::string SaveStateToMemory();
bool LoadStateFromMemory(const char* json, size_t length = 0);
const char* GetLastError();

// immediate-mode node canvas
bool BeginNodeEditor(const char* id, const Vec2& size = Vec2(-1, 300));
void EndNodeEditor();
bool BeginNode(int node_id, const char* title, Vec2* position, const Vec2& size = Vec2(180, 120));
NodePin NodeInput(const char* label);
NodePin NodeOutput(const char* label);
void EndNode();
void NodeLink(const NodePin& from, const NodePin& to, U32 color = 0);

// helpers
Vec2 CalcTextSize(const char* text, const char* text_end = 0);
U32  GetColorU32(int idx);

// diagnostics and validation
const FrameMetrics& GetFrameMetrics();
void SetDebugLogCallback(DebugLogCallback callback, void* user_data = 0);
bool ValidateState();
void ShowMetricsWindow(bool* p_open = 0);

// demo (osgui_demo.cpp)
void ShowDemoWindow(bool* p_open = 0);
void ShowNodeEditorDemo(bool* p_open = 0);
void ShowThemeEditor(bool* p_open = 0);

} // namespace og
