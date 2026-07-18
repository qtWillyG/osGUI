#include "osgui.h"

#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include <limits>
#include <string>

struct TestRunner {
    int checks;
    int failures;

    TestRunner() : checks(0), failures(0) {}

    void Check(bool condition, const char* expression, const char* file, int line) {
        ++checks;
        if (condition)
            return;

        ++failures;
        printf("FAIL %s:%d: %s\n", file, line, expression);
    }
};

static TestRunner g_tests;

#define CHECK(expression) g_tests.Check((expression), #expression, __FILE__, __LINE__)

static bool NearlyEqual(float lhs, float rhs, float tolerance = 0.001f) {
    return fabsf(lhs - rhs) <= tolerance;
}

static void InstallTestFont() {
    og::FontAtlas& atlas = og::GetFontAtlas();
    static unsigned char pixels[4] = {255, 255, 255, 255};

    atlas.pixels = pixels;
    atlas.width = 2;
    atlas.height = 2;
    atlas.line_height = 16.0f;
    atlas.ascent = 12.0f;
    atlas.tex_id = (og::TextureID)1;
    atlas.white_uv = og::Vec2(0.25f, 0.25f);
    atlas.glyph_map.clear();

    for (int codepoint = 0; codepoint < 128; ++codepoint) {
        og::Glyph& glyph = atlas.glyphs[codepoint];
        glyph.advance = 8.0f;
        glyph.x0 = 0.0f;
        glyph.y0 = 0.0f;
        glyph.x1 = 8.0f;
        glyph.y1 = 16.0f;
        glyph.u0 = 0.0f;
        glyph.v0 = 0.0f;
        glyph.u1 = 1.0f;
        glyph.v1 = 1.0f;
        atlas.glyph_valid[codepoint] = codepoint >= 32;
        if (atlas.glyph_valid[codepoint])
            atlas.glyph_map[(unsigned int)codepoint] = glyph;
    }

    atlas.glyph_map[0x03A9] = atlas.glyphs[(int)'?'];
    atlas.glyph_map[0x1F600] = atlas.glyphs[(int)'?'];
}

static void ConfigureContext(const og::Vec2& display_size = og::Vec2(1280.0f, 960.0f)) {
    og::IO& io = og::GetIO();
    io.display_size = display_size;
    io.framebuffer_scale = og::Vec2(1.0f, 1.0f);
    io.dpi_scale = 1.0f;
    io.delta_time = 1.0f / 60.0f;
    io.mouse_pos = og::Vec2(-1000.0f, -1000.0f);
    io.app_focused = true;
}

static bool BeginTestWindow(const char* name, const og::Vec2& position,
                            const og::Vec2& size) {
    og::SetNextWindowPos(position, og::Cond_Always);
    og::SetNextWindowSize(size, og::Cond_Always);
    const bool visible = og::Begin(
        name,
        0,
        og::WindowFlags_NoMove | og::WindowFlags_NoResize | og::WindowFlags_NoCollapse);
    CHECK(visible);
    return visible;
}

static void CountDebugMessage(const char* message, void* user_data) {
    int* count = static_cast<int*>(user_data);
    if (message && count)
        ++*count;
}

static void TestVersionAndLayout() {
    CHECK(strcmp(og::GetVersion(), OSGUI_VERSION) == 0);
    CHECK(OSGUI_VERSION_NUM == 20000);
    CHECK(OG_CHECKVERSION());
    CHECK(sizeof(og::TextureID) == sizeof(void*));
    CHECK(sizeof(og::DrawIdx) == sizeof(uint32_t));

    int debug_messages = 0;
    og::SetDebugLogCallback(CountDebugMessage, &debug_messages);
    CHECK(!og::DebugCheckVersionAndDataLayout(
        "not-the-linked-version",
        sizeof(og::IO),
        sizeof(og::Style),
        sizeof(og::DrawVert),
        sizeof(og::DrawIdx)));
    CHECK(debug_messages == 1);
    CHECK(strstr(og::GetLastError(), "mismatch") != 0);
    og::SetDebugLogCallback(0);
}

static void TestNestedClipIntersectionAndCompaction() {
    og::DrawList clipped;
    clipped.Clear();
    clipped.cur_tex = (og::TextureID)7;
    clipped.white_uv = og::Vec2(0.5f, 0.5f);

    clipped.PushClipRect(og::Vec4(0.0f, 0.0f, 100.0f, 100.0f), false);
    clipped.PushClipRect(og::Vec4(-20.0f, 20.0f, 120.0f, 80.0f), true);
    CHECK(clipped.clip_stack.size() == 2);
    CHECK(NearlyEqual(clipped.clip_stack.back().x, 0.0f));
    CHECK(NearlyEqual(clipped.clip_stack.back().y, 20.0f));
    CHECK(NearlyEqual(clipped.clip_stack.back().z, 100.0f));
    CHECK(NearlyEqual(clipped.clip_stack.back().w, 80.0f));

    clipped.PushClipRect(og::Vec4(140.0f, 140.0f, 180.0f, 180.0f), true);
    CHECK(NearlyEqual(clipped.clip_stack.back().x, clipped.clip_stack.back().z));
    CHECK(NearlyEqual(clipped.clip_stack.back().y, clipped.clip_stack.back().w));
    clipped.PopClipRect();
    clipped.PopClipRect();
    clipped.PopClipRect();

    og::DrawList compacted;
    compacted.Clear();
    compacted.cur_tex = (og::TextureID)11;
    compacted.white_uv = og::Vec2(0.5f, 0.5f);
    const og::Vec4 shared_clip(0.0f, 0.0f, 64.0f, 64.0f);
    compacted.PushClipRect(shared_clip, false);
    compacted.AddRectFilled(og::Vec2(2.0f, 2.0f), og::Vec2(12.0f, 12.0f), OG_COL32_WHITE);
    compacted.PushClipRect(shared_clip, true);
    compacted.AddRectFilled(og::Vec2(16.0f, 2.0f), og::Vec2(26.0f, 12.0f), OG_COL32_WHITE);
    compacted.PopClipRect();

    CHECK(compacted.cmds.size() >= 3);
    compacted.CompactCommands();
    CHECK(compacted.cmds.size() == 1);
    if (!compacted.cmds.empty())
        CHECK(compacted.cmds[0].elem_count == 12);
    CHECK(compacted.idx.size() == 12);
    CHECK(compacted.vtx.size() == 8);
}

static void TestMultipleContexts(og::Context* primary) {
    og::SetCurrentContext(primary);
    const float primary_rounding = og::GetStyle().frame_rounding;

    og::Context* secondary = og::CreateContext();
    CHECK(secondary != 0);
    CHECK(og::GetCurrentContext() == secondary);
    InstallTestFont();
    ConfigureContext(og::Vec2(640.0f, 480.0f));
    og::GetStyle().frame_rounding = 19.0f;

    og::SetCurrentContext(primary);
    CHECK(og::GetCurrentContext() == primary);
    CHECK(NearlyEqual(og::GetStyle().frame_rounding, primary_rounding));

    og::SetCurrentContext(secondary);
    CHECK(NearlyEqual(og::GetStyle().frame_rounding, 19.0f));
    og::SetCurrentContext(primary);
    og::DestroyContext(secondary);
    CHECK(og::GetCurrentContext() == primary);
}

static void TestLosslessQueuedInputPulses(og::Context* primary) {
    og::Context* pulse_context = og::CreateContext();
    CHECK(pulse_context != 0);
    if (!pulse_context) {
        og::SetCurrentContext(primary);
        return;
    }

    InstallTestFont();
    ConfigureContext(og::Vec2(640.0f, 480.0f));

    og::IO& io = og::GetIO();
    const og::Vec2 queued_mouse_position(123.5f, 234.25f);
    io.AddKeyEvent(og::Key_F3, true);
    io.AddKeyEvent(og::Key_F3, false);
    io.AddMousePosEvent(queued_mouse_position.x, queued_mouse_position.y);
    io.AddMouseButtonEvent(1, true);
    io.AddMouseButtonEvent(1, false);

    og::NewFrame();
    CHECK(og::GetFrameMetrics().input_events == 5);
    CHECK(og::IsKeyPressed(og::Key_F3));
    CHECK(!og::IsKeyDown(og::Key_F3));
    CHECK(og::IsMouseClicked(1));
    CHECK(og::IsMouseReleased(1));
    CHECK(!og::IsMouseDown(1));
    CHECK(NearlyEqual(og::GetMousePos().x, queued_mouse_position.x));
    CHECK(NearlyEqual(og::GetMousePos().y, queued_mouse_position.y));
    CHECK(NearlyEqual(og::GetContext().mouse_clicked_pos[1].x, queued_mouse_position.x));
    CHECK(NearlyEqual(og::GetContext().mouse_clicked_pos[1].y, queued_mouse_position.y));
    og::Render();
    CHECK(io.input_events.empty());

    og::DestroyContext(pulse_context);
    og::SetCurrentContext(primary);
    CHECK(og::GetCurrentContext() == primary);
}

struct NavigationFrameResult {
    og::ID first;
    og::ID second;
    og::ID third;

    NavigationFrameResult() : first(0), second(0), third(0) {}
};

static NavigationFrameResult SubmitNavigationFrame() {
    NavigationFrameResult result;
    og::NewFrame();
    const bool visible = BeginTestWindow(
        "Tests / Queued navigation",
        og::Vec2(30.0f, 30.0f),
        og::Vec2(440.0f, 260.0f));
    if (visible) {
        og::Button("First target");
        result.first = og::GetItemID();
        og::Button("Second target");
        result.second = og::GetItemID();
        og::Button("Third target");
        result.third = og::GetItemID();
    }
    og::End();
    og::Render();
    return result;
}

static void TestCompleteShiftTabChord(og::Context* primary) {
    og::Context* navigation_context = og::CreateContext();
    CHECK(navigation_context != 0);
    if (!navigation_context) {
        og::SetCurrentContext(primary);
        return;
    }

    InstallTestFont();
    ConfigureContext(og::Vec2(720.0f, 480.0f));

    const NavigationFrameResult initial = SubmitNavigationFrame();
    CHECK(initial.first != 0);
    CHECK(initial.second != 0);
    CHECK(initial.third != 0);
    CHECK(og::GetContext().nav_id == initial.first);

    og::IO& io = og::GetIO();
    io.AddKeyEvent(og::Key_Shift, true);
    io.AddKeyEvent(og::Key_Tab, true);
    io.AddKeyEvent(og::Key_Tab, false);
    io.AddKeyEvent(og::Key_Shift, false);

    const NavigationFrameResult shifted = SubmitNavigationFrame();
    CHECK(shifted.first == initial.first);
    CHECK(shifted.second == initial.second);
    CHECK(shifted.third == initial.third);
    CHECK(!og::IsKeyDown(og::Key_Shift));
    CHECK(!og::IsKeyDown(og::Key_Tab));
    CHECK(og::GetContext().nav_id == shifted.third);

    og::DestroyContext(navigation_context);
    og::SetCurrentContext(primary);
    CHECK(og::GetCurrentContext() == primary);
}

static void TestIDScopesAndConflictMetrics() {
    og::GetIO().mouse_pos = og::Vec2(-1000.0f, -1000.0f);
    og::NewFrame();

    og::ID first_id = 0;
    og::ID duplicate_id = 0;
    og::ID scoped_one = 0;
    og::ID scoped_two = 0;
    const bool visible = BeginTestWindow(
        "Tests / Identity",
        og::Vec2(20.0f, 20.0f),
        og::Vec2(420.0f, 320.0f));
    if (visible) {
        og::Button("Repeated control");
        first_id = og::GetItemID();
        og::Button("Repeated control");
        duplicate_id = og::GetItemID();

        og::PushID(1);
        og::Button("Repeated control");
        scoped_one = og::GetItemID();
        og::PopID();

        og::PushID(2);
        og::Button("Repeated control");
        scoped_two = og::GetItemID();
        og::PopID();
    }
    og::End();

    CHECK(first_id != 0);
    CHECK(first_id == duplicate_id);
    CHECK(scoped_one != scoped_two);
    CHECK(scoped_one != first_id);
    CHECK(og::ValidateState());
    og::Render();

    const og::FrameMetrics& metrics = og::GetFrameMetrics();
    CHECK(metrics.id_conflicts == 1);
    CHECK(metrics.items_submitted >= 4);
    CHECK(metrics.draw_commands > 0);
}

static void TestStyleDisabledAndItemQueries() {
    og::Style& style = og::GetStyle();
    const og::U32 original_button = style.colors[og::Col_Button];
    const float original_rounding = style.frame_rounding;
    const og::Vec2 original_padding = style.frame_padding;

    og::PushStyleColor(og::Col_Button, OG_COL32(1, 2, 3, 255));
    CHECK(style.colors[og::Col_Button] == OG_COL32(1, 2, 3, 255));
    og::PopStyleColor();
    CHECK(style.colors[og::Col_Button] == original_button);

    og::PushStyleVar(og::StyleVar_FrameRounding, 17.0f);
    CHECK(NearlyEqual(style.frame_rounding, 17.0f));
    og::PopStyleVar();
    CHECK(NearlyEqual(style.frame_rounding, original_rounding));

    og::PushStyleVar(og::StyleVar_FramePadding, og::Vec2(11.0f, 7.0f));
    CHECK(NearlyEqual(style.frame_padding.x, 11.0f));
    CHECK(NearlyEqual(style.frame_padding.y, 7.0f));
    og::PopStyleVar();
    CHECK(NearlyEqual(style.frame_padding.x, original_padding.x));
    CHECK(NearlyEqual(style.frame_padding.y, original_padding.y));

    og::NewFrame();
    bool disabled_pressed = false;
    bool disabled_visible = false;
    bool disabled_status = false;
    og::ID disabled_id = 0;
    og::Vec2 disabled_size;

    const bool visible = BeginTestWindow(
        "Tests / Scoped presentation",
        og::Vec2(30.0f, 30.0f),
        og::Vec2(460.0f, 300.0f));
    if (visible) {
        og::BeginDisabled(true);
        disabled_pressed = og::Button("Unavailable action", og::Vec2(180.0f, 34.0f));
        disabled_visible = og::IsItemVisible();
        disabled_id = og::GetItemID();
        disabled_size = og::GetItemRectSize();
        disabled_status = (og::GetContext().last_item.status_flags & og::ItemStatus_Disabled) != 0;
        CHECK(!og::IsItemHovered());
        CHECK(!og::IsItemActive());
        CHECK(!og::IsItemClicked());
        og::EndDisabled();

        og::BeginDisabled(false);
        og::Button("Available action");
        CHECK((og::GetContext().last_item.status_flags & og::ItemStatus_Disabled) == 0);
        og::EndDisabled();
    }
    og::End();

    CHECK(!disabled_pressed);
    CHECK(disabled_visible);
    CHECK(disabled_status);
    CHECK(disabled_id != 0);
    CHECK(disabled_size.x > 0.0f && disabled_size.y > 0.0f);
    CHECK(og::GetContext().disabled_depth == 0);
    CHECK(og::GetContext().disabled_stack.empty());
    CHECK(og::ValidateState());
    og::Render();
}

static void TestClosedWindowContract() {
    bool open = false;
    og::NewFrame();
    og::SetNextWindowPos(og::Vec2(40.0f, 40.0f), og::Cond_Always);
    og::SetNextWindowSize(og::Vec2(300.0f, 220.0f), og::Cond_Always);
    const bool visible = og::Begin("Tests / Closed", &open);
    CHECK(!visible);
    CHECK(!open);
    og::End();
    CHECK(og::ValidateState());
    og::Render();
    CHECK(og::GetFrameMetrics().active_windows == 0);
}

static void SubmitVirtualChild(int* display_start, int* display_end,
                               int* submitted_rows, og::Vec4* child_rect) {
    const bool child_visible = og::BeginChild(
        "virtual-child",
        og::Vec2(260.0f, 110.0f),
        og::ChildFlags_Borders | og::ChildFlags_AlwaysUseWindowPadding);
    CHECK(child_visible);

    if (child_visible) {
        const float row_step = 24.0f;
        og::ListClipper clipper;
        clipper.Begin(200, row_step);
        while (clipper.Step()) {
            *display_start = clipper.display_start;
            *display_end = clipper.display_end;
            for (int row = clipper.display_start; row < clipper.display_end; ++row) {
                og::PushID(row);
                og::Selectable("Virtual row", false,
                               og::Vec2(-1.0f, row_step - og::GetStyle().item_spacing.y));
                CHECK(og::GetItemID() != 0);
                og::PopID();
                ++*submitted_rows;
            }
        }
        clipper.End();
    }
    og::EndChild();

    const og::Vec2 rect_min = og::GetItemRectMin();
    const og::Vec2 rect_max = og::GetItemRectMax();
    *child_rect = og::Vec4(rect_min.x, rect_min.y, rect_max.x, rect_max.y);
}

static void TestChildScrollingAndListClipper() {
    int first_start = -1;
    int first_end = -1;
    int first_submitted = 0;
    og::Vec4 child_rect;

    og::GetIO().mouse_pos = og::Vec2(-1000.0f, -1000.0f);
    og::NewFrame();
    const bool first_visible = BeginTestWindow(
        "Tests / Virtual child",
        og::Vec2(20.0f, 20.0f),
        og::Vec2(360.0f, 260.0f));
    if (first_visible)
        SubmitVirtualChild(&first_start, &first_end, &first_submitted, &child_rect);
    og::End();
    CHECK(og::ValidateState());
    og::Render();

    CHECK(first_start == 0);
    CHECK(first_end > first_start);
    CHECK(first_end < 200);
    CHECK(first_submitted == first_end - first_start);

    og::IO& io = og::GetIO();
    io.AddMousePosEvent((child_rect.x + child_rect.z) * 0.5f,
                        (child_rect.y + child_rect.w) * 0.5f);
    io.AddMouseWheelEvent(0.0f, -4.0f);

    int scrolled_start = -1;
    int scrolled_end = -1;
    int scrolled_submitted = 0;
    og::Vec4 scrolled_rect;
    og::NewFrame();
    const bool second_visible = BeginTestWindow(
        "Tests / Virtual child",
        og::Vec2(20.0f, 20.0f),
        og::Vec2(360.0f, 260.0f));
    if (second_visible)
        SubmitVirtualChild(&scrolled_start, &scrolled_end, &scrolled_submitted, &scrolled_rect);
    og::End();
    og::Render();

    CHECK(scrolled_start > 0);
    CHECK(scrolled_end > scrolled_start);
    CHECK(scrolled_submitted == scrolled_end - scrolled_start);
    CHECK(NearlyEqual(scrolled_rect.x, child_rect.x));
}

static void TestMemoryAndFilePersistence() {
    og::Context* source_context = og::GetCurrentContext();
    const char* escaped_window_name = "Tests / Escaped {profile}\\";

    og::SetUIScale(1.25f);
    og::GetStyle().frame_rounding = 13.25f;
    og::GetStyle().item_inner_spacing = og::Vec2(7.25f, 4.5f);
    og::SetReducedMotion(true);

    og::GetIO().mouse_pos = og::Vec2(-1000.0f, -1000.0f);
    og::NewFrame();
    const bool visible = BeginTestWindow(
        "Tests / Persistence target",
        og::Vec2(77.0f, 88.0f),
        og::Vec2(456.0f, 321.0f));
    if (visible)
        og::Text("Persistent content");
    og::End();

    og::SetNextWindowPos(og::Vec2(612.0f, 214.0f), og::Cond_Always);
    og::SetNextWindowSize(og::Vec2(333.0f, 177.0f), og::Cond_Always);
    const bool escaped_visible = og::Begin(
        escaped_window_name,
        0,
        og::WindowFlags_NoMove | og::WindowFlags_NoResize | og::WindowFlags_NoCollapse);
    CHECK(escaped_visible);
    if (escaped_visible)
        og::Text("A brace and terminal backslash exercise JSON string parsing.");
    og::End();
    og::Render();

    const std::string saved = og::SaveStateToMemory();
    CHECK(!saved.empty());
    CHECK(saved.find("\"version\": 2") != std::string::npos);
    CHECK(saved.find("Tests / Persistence target") != std::string::npos);
    CHECK(saved.find("Tests / Escaped {profile}\\\\") != std::string::npos);

    og::SetUIScale(0.8f);
    og::GetStyle().frame_rounding = 2.0f;
    og::GetStyle().item_inner_spacing = og::Vec2(1.0f, 2.0f);
    og::SetReducedMotion(false);
    CHECK(og::LoadStateFromMemory(saved.data(), saved.size()));
    CHECK(NearlyEqual(og::GetUIScale(), 1.25f));
    CHECK(NearlyEqual(og::GetStyle().frame_rounding, 13.25f));
    CHECK(NearlyEqual(og::GetStyle().item_inner_spacing.x, 7.25f));
    CHECK(NearlyEqual(og::GetStyle().item_inner_spacing.y, 4.5f));
    CHECK(og::IsReducedMotion());

    og::NewFrame();
    const bool restored_visible = og::Begin("Tests / Persistence target");
    CHECK(restored_visible);
    if (restored_visible) {
        CHECK(NearlyEqual(og::GetWindowPos().x, 77.0f));
        CHECK(NearlyEqual(og::GetWindowPos().y, 88.0f));
        CHECK(NearlyEqual(og::GetWindowSize().x, 456.0f));
        CHECK(NearlyEqual(og::GetWindowSize().y, 321.0f));
    }
    og::End();
    og::Render();

    og::Context* restore_context = og::CreateContext();
    CHECK(restore_context != 0);
    if (restore_context) {
        InstallTestFont();
        ConfigureContext();
        og::GetStyle().item_inner_spacing = og::Vec2(0.5f, 0.75f);
        og::SetReducedMotion(false);

        CHECK(og::LoadStateFromMemory(saved.data(), saved.size()));
        CHECK(NearlyEqual(og::GetStyle().item_inner_spacing.x, 7.25f));
        CHECK(NearlyEqual(og::GetStyle().item_inner_spacing.y, 4.5f));
        CHECK(og::IsReducedMotion());

        og::NewFrame();
        const bool escaped_restored = og::Begin(
            escaped_window_name,
            0,
            og::WindowFlags_NoMove | og::WindowFlags_NoResize | og::WindowFlags_NoCollapse);
        CHECK(escaped_restored);
        if (escaped_restored) {
            CHECK(NearlyEqual(og::GetWindowPos().x, 612.0f));
            CHECK(NearlyEqual(og::GetWindowPos().y, 214.0f));
            CHECK(NearlyEqual(og::GetWindowSize().x, 333.0f));
            CHECK(NearlyEqual(og::GetWindowSize().y, 177.0f));
        }
        og::End();
        og::Render();

        og::DestroyContext(restore_context);
    }
    og::SetCurrentContext(source_context);
    CHECK(og::GetCurrentContext() == source_context);

    const char* state_path = "osgui-v2-test-state.json";
    CHECK(og::SaveStateJSON(state_path));
    og::GetStyle().frame_rounding = 1.0f;
    og::GetStyle().item_inner_spacing = og::Vec2(0.25f, 0.5f);
    og::SetReducedMotion(false);
    CHECK(og::LoadStateJSON(state_path));
    CHECK(NearlyEqual(og::GetStyle().frame_rounding, 13.25f));
    CHECK(NearlyEqual(og::GetStyle().item_inner_spacing.x, 7.25f));
    CHECK(NearlyEqual(og::GetStyle().item_inner_spacing.y, 4.5f));
    CHECK(og::IsReducedMotion());
    CHECK(remove(state_path) == 0);

    const char* scoped_state =
        "{"
        "\"metadata\":{\"uiScale\":3.75,\"reducedMotion\":true},"
        "\"version\":2,\"uiScale\":1.5,\"reducedMotion\":false,"
        "\"theme\":{\"metadata\":{\"frameRounding\":99},\"frameRounding\":8.5},"
        "\"windows\":[{\"name\":\"Tests / Scoped state\","
        "\"metadata\":{\"x\":999,\"collapsed\":true},"
        "\"x\":123,\"y\":234,\"width\":345,\"height\":210,"
        "\"dock\":0,\"collapsed\":false}]"
        "}";
    CHECK(og::LoadStateFromMemory(scoped_state, strlen(scoped_state)));
    CHECK(NearlyEqual(og::GetUIScale(), 1.5f));
    CHECK(!og::IsReducedMotion());
    CHECK(NearlyEqual(og::GetStyle().frame_rounding, 8.5f));

    og::NewFrame();
    const bool scoped_visible = og::Begin("Tests / Scoped state");
    CHECK(scoped_visible);
    if (scoped_visible) {
        CHECK(NearlyEqual(og::GetWindowPos().x, 123.0f));
        CHECK(NearlyEqual(og::GetWindowPos().y, 234.0f));
        CHECK(NearlyEqual(og::GetWindowSize().x, 345.0f));
        CHECK(NearlyEqual(og::GetWindowSize().y, 210.0f));
    }
    og::End();
    og::Render();

    const char* version_one_state =
        "{\"version\":1,\"uiScale\":1.1,"
        "\"theme\":{\"frameRounding\":4.25},\"windows\":[]}";
    CHECK(og::LoadStateFromMemory(version_one_state, strlen(version_one_state)));
    CHECK(NearlyEqual(og::GetUIScale(), 1.1f));
    CHECK(NearlyEqual(og::GetStyle().frame_rounding, 4.25f));

    CHECK(!og::LoadStateFromMemory("not json", 8));
    CHECK(og::GetLastError()[0] != 0);

    const std::string before_failed_load = og::SaveStateToMemory();
    const char* malformed_partial_state =
        "{\"version\":2,\"uiScale\":2,\"frameRounding\":99,\"colors\":[1}";
    CHECK(!og::LoadStateFromMemory(malformed_partial_state, strlen(malformed_partial_state)));
    CHECK(og::GetLastError()[0] != 0);
    const std::string after_failed_load = og::SaveStateToMemory();
    CHECK(after_failed_load == before_failed_load);

    const char* malformed_inputs[] = {
        "{\"version\":2,\"uiScale\":+1}",
        "{\"version\":2,\"uiScale\":1,\"uiScale\":2}",
        "{\"version\":2,\"theme\":[]}",
        "{\"version\":2,\"windows\":[{\"name\":\"trailing\"},]}",
        "{\"version\":2,\"windows\":[{\"name\":\"bad\\q\"}]}"
    };
    for (size_t input_index = 0;
         input_index < sizeof(malformed_inputs) / sizeof(malformed_inputs[0]);
         ++input_index) {
        const std::string state_before_rejection = og::SaveStateToMemory();
        CHECK(!og::LoadStateFromMemory(
            malformed_inputs[input_index], strlen(malformed_inputs[input_index])));
        CHECK(og::GetLastError()[0] != 0);
        CHECK(og::SaveStateToMemory() == state_before_rejection);
    }
}

static void TestPointerWidthTextureAndImageCommand() {
    int texture_token = 0;
    const og::TextureID texture_id = reinterpret_cast<og::TextureID>(&texture_token);
    CHECK(texture_id != 0);
    CHECK(sizeof(texture_id) == sizeof(&texture_token));

    og::NewFrame();
    const bool visible = BeginTestWindow(
        "Tests / Texture identity",
        og::Vec2(45.0f, 45.0f),
        og::Vec2(360.0f, 240.0f));
    if (visible)
        og::Image(texture_id, og::Vec2(48.0f, 32.0f));
    og::End();
    og::Render();

    bool found_image_command = false;
    const og::DrawData* draw_data = og::GetDrawData();
    for (size_t list_index = 0; list_index < draw_data->lists.size(); ++list_index) {
        const og::DrawList* draw_list = draw_data->lists[list_index];
        for (size_t command_index = 0; command_index < draw_list->cmds.size(); ++command_index) {
            const og::DrawCmd& command = draw_list->cmds[command_index];
            if (command.tex_id == texture_id && command.elem_count == 6)
                found_image_command = true;
        }
    }
    CHECK(found_image_command);

    og::DrawDataSnapshot snapshot;
    snapshot.Capture(draw_data);
    CHECK(snapshot.GetDrawData()->total_vtx == draw_data->total_vtx);
    CHECK(snapshot.GetDrawData()->total_idx == draw_data->total_idx);
    CHECK(snapshot.GetDrawData()->lists.size() == draw_data->lists.size());
}

static void SubmitUTF8Frame(char* text, int text_capacity) {
    og::NewFrame();
    const bool visible = BeginTestWindow(
        "Tests / UTF-8 queue",
        og::Vec2(25.0f, 25.0f),
        og::Vec2(460.0f, 240.0f));
    if (visible)
        og::InputText("Queued text", text, text_capacity);
    og::End();
    og::Render();
}

static void TestQueuedUTF8AndBackspace(og::Context* primary) {
    og::Context* text_context = og::CreateContext();
    CHECK(text_context != 0);
    InstallTestFont();
    ConfigureContext(og::Vec2(720.0f, 480.0f));

    char text[64] = "";
    SubmitUTF8Frame(text, (int)sizeof(text));

    og::IO& io = og::GetIO();
    io.AddKeyEvent(og::Key_Enter, true);
    SubmitUTF8Frame(text, (int)sizeof(text));

    io.AddKeyEvent(og::Key_Enter, false);
    io.AddInputCharacter(0x03A9);
    io.AddInputCharacter(0x1F600);
    SubmitUTF8Frame(text, (int)sizeof(text));
    CHECK(strcmp(text, "\xCE\xA9\xF0\x9F\x98\x80") == 0);
    CHECK(og::GetFrameMetrics().input_events == 3);

    io.AddKeyEvent(og::Key_Backspace, true);
    SubmitUTF8Frame(text, (int)sizeof(text));
    CHECK(strcmp(text, "\xCE\xA9") == 0);
    CHECK(io.input_chars.empty());
    CHECK(io.input_events.empty());

    io.AddKeyEvent(og::Key_Backspace, false);
    SubmitUTF8Frame(text, (int)sizeof(text));

    og::DestroyContext(text_context);
    og::SetCurrentContext(primary);
    CHECK(og::GetCurrentContext() == primary);
}

static bool SubmitEditingFrame(char* text, int text_capacity, og::ID* item_id) {
    bool changed = false;
    og::NewFrame();
    const bool visible = BeginTestWindow(
        "Tests / InputText editing",
        og::Vec2(25.0f, 25.0f),
        og::Vec2(520.0f, 240.0f));
    if (visible) {
        changed = og::InputText("Editing text", text, text_capacity);
        if (item_id)
            *item_id = og::GetItemID();
    }
    og::End();
    og::Render();
    return changed;
}

static void TestOrderedTextActionsAndCompleteCtrlZChord(og::Context* primary) {
    og::Context* ordered_context = og::CreateContext();
    CHECK(ordered_context != 0);
    if (!ordered_context) {
        og::SetCurrentContext(primary);
        return;
    }

    InstallTestFont();
    ConfigureContext(og::Vec2(760.0f, 520.0f));

    char text[64] = "x";
    og::ID edit_id = 0;
    CHECK(!SubmitEditingFrame(text, (int)sizeof(text), &edit_id));
    CHECK(edit_id != 0);
    CHECK(og::GetContext().nav_id == edit_id);

    og::IO& io = og::GetIO();
    io.AddKeyEvent(og::Key_Enter, true);
    io.AddKeyEvent(og::Key_Enter, false);
    CHECK(!SubmitEditingFrame(text, (int)sizeof(text), &edit_id));
    CHECK(og::GetContext().text_active_id == edit_id);

    // The character must be inserted before Backspace is dispatched. A
    // command-first implementation incorrectly turns "x" into "a" here.
    io.AddInputCharacter((unsigned int)'a');
    io.AddKeyEvent(og::Key_Backspace, true);
    io.AddKeyEvent(og::Key_Backspace, false);
    SubmitEditingFrame(text, (int)sizeof(text), &edit_id);
    CHECK(strcmp(text, "x") == 0);

    io.AddInputCharacter((unsigned int)'y');
    CHECK(SubmitEditingFrame(text, (int)sizeof(text), &edit_id));
    CHECK(strcmp(text, "xy") == 0);

    // The complete chord starts and ends before NewFrame. Undo must use the
    // modifier snapshot attached to the queued Z press, not final key state.
    io.AddKeyEvent(og::Key_Ctrl, true);
    io.AddKeyEvent(og::Key_Z, true);
    io.AddKeyEvent(og::Key_Z, false);
    io.AddKeyEvent(og::Key_Ctrl, false);
    CHECK(SubmitEditingFrame(text, (int)sizeof(text), &edit_id));
    CHECK(strcmp(text, "x") == 0);
    CHECK(!og::IsKeyDown(og::Key_Ctrl));
    CHECK(!og::IsKeyDown(og::Key_Z));

    og::DestroyContext(ordered_context);
    og::SetCurrentContext(primary);
    CHECK(og::GetCurrentContext() == primary);
}

static void TestInputTextSelectionHistoryAndWords(og::Context* primary) {
    og::Context* edit_context = og::CreateContext();
    CHECK(edit_context != 0);
    if (!edit_context) {
        og::SetCurrentContext(primary);
        return;
    }

    InstallTestFont();
    ConfigureContext(og::Vec2(760.0f, 520.0f));

    char text[128] = "seed value";
    og::ID edit_id = 0;
    CHECK(!SubmitEditingFrame(text, (int)sizeof(text), &edit_id));
    CHECK(edit_id != 0);
    CHECK(og::GetContext().nav_id == edit_id);

    og::IO& io = og::GetIO();
    io.AddKeyEvent(og::Key_Enter, true);
    CHECK(!SubmitEditingFrame(text, (int)sizeof(text), &edit_id));
    CHECK(og::GetContext().text_active_id == edit_id);

    io.AddKeyEvent(og::Key_Enter, false);
    CHECK(!SubmitEditingFrame(text, (int)sizeof(text), &edit_id));

    io.AddKeyEvent(og::Key_Ctrl, true);
    io.AddKeyEvent(og::Key_A, true);
    const char* replacement = "alpha beta gamma";
    for (const char* character = replacement; *character; ++character)
        io.AddInputCharacter((unsigned int)(unsigned char)*character);

    CHECK(SubmitEditingFrame(text, (int)sizeof(text), &edit_id));
    CHECK(strcmp(text, replacement) == 0);
    CHECK(og::GetContext().text_cursor[edit_id] == (int)strlen(replacement));
    CHECK(og::GetContext().text_selection_anchor[edit_id] == (int)strlen(replacement));
    CHECK((og::GetContext().last_item.status_flags & og::ItemStatus_Edited) != 0);

    bool emitted_text_event = false;
    const std::vector<og::Event>& replacement_events = og::GetEvents();
    for (size_t event_index = 0; event_index < replacement_events.size(); ++event_index) {
        if (replacement_events[event_index].type == og::Event_TextChanged &&
            replacement_events[event_index].id == edit_id) {
            emitted_text_event = true;
        }
    }
    CHECK(emitted_text_event);

    io.AddKeyEvent(og::Key_A, false);
    CHECK(!SubmitEditingFrame(text, (int)sizeof(text), &edit_id));

    io.AddKeyEvent(og::Key_Z, true);
    CHECK(SubmitEditingFrame(text, (int)sizeof(text), &edit_id));
    CHECK(strcmp(text, "seed value") == 0);
    CHECK(og::GetContext().text_cursor[edit_id] == (int)strlen("seed value"));
    CHECK(og::GetContext().text_selection_anchor[edit_id] == (int)strlen("seed value"));

    io.AddKeyEvent(og::Key_Z, false);
    CHECK(!SubmitEditingFrame(text, (int)sizeof(text), &edit_id));

    io.AddKeyEvent(og::Key_Y, true);
    CHECK(SubmitEditingFrame(text, (int)sizeof(text), &edit_id));
    CHECK(strcmp(text, replacement) == 0);
    CHECK(og::GetContext().text_cursor[edit_id] == (int)strlen(replacement));

    io.AddKeyEvent(og::Key_Y, false);
    CHECK(!SubmitEditingFrame(text, (int)sizeof(text), &edit_id));

    io.AddKeyEvent(og::Key_LeftArrow, true);
    CHECK(!SubmitEditingFrame(text, (int)sizeof(text), &edit_id));
    CHECK(og::GetContext().text_cursor[edit_id] == 11);
    CHECK(og::GetContext().text_selection_anchor[edit_id] == 11);

    io.AddKeyEvent(og::Key_LeftArrow, false);
    CHECK(!SubmitEditingFrame(text, (int)sizeof(text), &edit_id));

    io.AddKeyEvent(og::Key_Shift, true);
    io.AddKeyEvent(og::Key_LeftArrow, true);
    CHECK(!SubmitEditingFrame(text, (int)sizeof(text), &edit_id));
    CHECK(og::GetContext().text_cursor[edit_id] == 6);
    CHECK(og::GetContext().text_selection_anchor[edit_id] == 11);

    io.AddKeyEvent(og::Key_LeftArrow, false);
    CHECK(!SubmitEditingFrame(text, (int)sizeof(text), &edit_id));

    io.AddKeyEvent(og::Key_Backspace, true);
    CHECK(SubmitEditingFrame(text, (int)sizeof(text), &edit_id));
    CHECK(strcmp(text, "alpha gamma") == 0);
    CHECK(og::GetContext().text_cursor[edit_id] == 6);
    CHECK(og::GetContext().text_selection_anchor[edit_id] == 6);

    io.AddKeyEvent(og::Key_Backspace, false);
    io.AddKeyEvent(og::Key_Shift, false);
    io.AddKeyEvent(og::Key_Ctrl, false);
    CHECK(!SubmitEditingFrame(text, (int)sizeof(text), &edit_id));

    og::DestroyContext(edit_context);
    og::SetCurrentContext(primary);
    CHECK(og::GetCurrentContext() == primary);
}

struct TextActivationFrameResult {
    og::ID id;
    bool returned;

    TextActivationFrameResult() : id(0), returned(false) {}
};

static TextActivationFrameResult SubmitTextActivationFrame(
    const char* window_name,
    const char* label,
    char* text,
    int text_capacity,
    bool multiline,
    int flags) {
    TextActivationFrameResult result;
    og::NewFrame();
    const bool visible = BeginTestWindow(
        window_name,
        og::Vec2(35.0f, 35.0f),
        og::Vec2(520.0f, 300.0f));
    if (visible) {
        if (multiline) {
            result.returned = og::InputTextMultiline(
                label,
                text,
                text_capacity,
                og::Vec2(300.0f, 90.0f),
                flags);
        } else {
            result.returned = og::InputText(label, text, text_capacity, flags);
        }
        result.id = og::GetItemID();
    }
    og::End();
    og::Render();
    return result;
}

static void TestNavigationActivationIsConsumed(og::Context* primary) {
    og::Context* multiline_context = og::CreateContext();
    CHECK(multiline_context != 0);
    if (!multiline_context) {
        og::SetCurrentContext(primary);
        return;
    }

    InstallTestFont();
    ConfigureContext(og::Vec2(760.0f, 520.0f));
    char multiline_text[64] = "line";
    const TextActivationFrameResult multiline_initial = SubmitTextActivationFrame(
        "Tests / Multiline nav activation",
        "Multiline editor",
        multiline_text,
        (int)sizeof(multiline_text),
        true,
        og::InputTextFlags_None);
    CHECK(multiline_initial.id != 0);
    CHECK(og::GetContext().nav_id == multiline_initial.id);

    og::IO& multiline_io = og::GetIO();
    multiline_io.AddKeyEvent(og::Key_Enter, true);
    multiline_io.AddKeyEvent(og::Key_Enter, false);
    const TextActivationFrameResult multiline_activated = SubmitTextActivationFrame(
        "Tests / Multiline nav activation",
        "Multiline editor",
        multiline_text,
        (int)sizeof(multiline_text),
        true,
        og::InputTextFlags_None);
    CHECK(!multiline_activated.returned);
    CHECK(strcmp(multiline_text, "line") == 0);
    CHECK(og::GetContext().text_active_id == multiline_activated.id);
    CHECK(og::GetContext().nav_activate_id == 0);

    og::DestroyContext(multiline_context);
    og::SetCurrentContext(primary);

    og::Context* submit_context = og::CreateContext();
    CHECK(submit_context != 0);
    if (!submit_context) {
        og::SetCurrentContext(primary);
        return;
    }

    InstallTestFont();
    ConfigureContext(og::Vec2(760.0f, 520.0f));
    char submit_text[64] = "submit me later";
    const TextActivationFrameResult submit_initial = SubmitTextActivationFrame(
        "Tests / Submit nav activation",
        "Submit editor",
        submit_text,
        (int)sizeof(submit_text),
        false,
        og::InputTextFlags_EnterReturnsTrue);
    CHECK(submit_initial.id != 0);
    CHECK(og::GetContext().nav_id == submit_initial.id);

    og::IO& submit_io = og::GetIO();
    submit_io.AddKeyEvent(og::Key_Enter, true);
    submit_io.AddKeyEvent(og::Key_Enter, false);
    const TextActivationFrameResult submit_activated = SubmitTextActivationFrame(
        "Tests / Submit nav activation",
        "Submit editor",
        submit_text,
        (int)sizeof(submit_text),
        false,
        og::InputTextFlags_EnterReturnsTrue);
    CHECK(!submit_activated.returned);
    CHECK(strcmp(submit_text, "submit me later") == 0);
    CHECK(og::GetContext().text_active_id == submit_activated.id);
    CHECK(og::GetContext().nav_activate_id == 0);

    og::DestroyContext(submit_context);
    og::SetCurrentContext(primary);
    CHECK(og::GetCurrentContext() == primary);
}

struct SliderPulseFrameResult {
    og::Vec4 rect;
    bool changed;

    SliderPulseFrameResult() : rect(), changed(false) {}
};

static SliderPulseFrameResult SubmitSliderPulseFrame(float* value) {
    SliderPulseFrameResult result;
    og::NewFrame();
    const bool visible = BeginTestWindow(
        "Tests / Queued slider pulse",
        og::Vec2(40.0f, 40.0f),
        og::Vec2(520.0f, 260.0f));
    if (visible) {
        result.changed = og::SliderFloat("Pulse target", value, 0.0f, 1.0f);
        const og::Vec2 minimum = og::GetItemRectMin();
        const og::Vec2 maximum = og::GetItemRectMax();
        result.rect = og::Vec4(minimum.x, minimum.y, maximum.x, maximum.y);
    }
    og::End();
    og::Render();
    return result;
}

static void TestQueuedSliderPulseCoordinates(og::Context* primary) {
    og::Context* slider_context = og::CreateContext();
    CHECK(slider_context != 0);
    if (!slider_context) {
        og::SetCurrentContext(primary);
        return;
    }

    InstallTestFont();
    ConfigureContext(og::Vec2(760.0f, 520.0f));
    float value = 0.05f;
    const SliderPulseFrameResult initial = SubmitSliderPulseFrame(&value);
    CHECK(initial.rect.z > initial.rect.x);

    const og::Vec2 target(
        initial.rect.x + (initial.rect.z - initial.rect.x) * 0.78f,
        (initial.rect.y + initial.rect.w) * 0.5f);
    og::IO& io = og::GetIO();
    io.AddMousePosEvent(target.x, target.y);
    io.AddMouseButtonEvent(0, true);
    io.AddMouseButtonEvent(0, false);
    io.AddMousePosEvent(-1000.0f, -1000.0f);

    const SliderPulseFrameResult pulsed = SubmitSliderPulseFrame(&value);
    CHECK(pulsed.changed);
    CHECK(value > 0.65f);
    CHECK(value < 0.90f);
    CHECK(!og::IsMouseDown(0));
    CHECK(NearlyEqual(og::GetContext().mouse_clicked_pos[0].x, target.x));
    CHECK(NearlyEqual(og::GetContext().mouse_released_pos[0].x, target.x));

    og::DestroyContext(slider_context);
    og::SetCurrentContext(primary);
    CHECK(og::GetCurrentContext() == primary);
}

struct ExtremeIntegerFrameResult {
    bool slider_changed;
    bool drag_changed;

    ExtremeIntegerFrameResult() : slider_changed(false), drag_changed(false) {}
};

static ExtremeIntegerFrameResult SubmitExtremeIntegerFrame(int* slider_value, int* drag_value) {
    ExtremeIntegerFrameResult result;
    og::NewFrame();
    const bool visible = BeginTestWindow(
        "Tests / Extreme integer stability",
        og::Vec2(45.0f, 45.0f),
        og::Vec2(560.0f, 280.0f));
    if (visible) {
        result.slider_changed = og::SliderInt(
            "Extreme slider",
            slider_value,
            std::numeric_limits<int>::min(),
            std::numeric_limits<int>::max());
        result.drag_changed = og::DragInt(
            "Extreme drag",
            drag_value,
            0.25f,
            std::numeric_limits<int>::min(),
            std::numeric_limits<int>::max());
    }
    og::End();
    og::Render();
    return result;
}

static void TestPassiveExtremeIntegerStability(og::Context* primary) {
    og::Context* integer_context = og::CreateContext();
    CHECK(integer_context != 0);
    if (!integer_context) {
        og::SetCurrentContext(primary);
        return;
    }

    InstallTestFont();
    ConfigureContext(og::Vec2(800.0f, 560.0f));
    const int expected_slider = std::numeric_limits<int>::max() - 3;
    const int expected_drag = std::numeric_limits<int>::min() + 3;
    int slider_value = expected_slider;
    int drag_value = expected_drag;

    const ExtremeIntegerFrameResult first = SubmitExtremeIntegerFrame(&slider_value, &drag_value);
    CHECK(!first.slider_changed);
    CHECK(!first.drag_changed);
    CHECK(slider_value == expected_slider);
    CHECK(drag_value == expected_drag);

    const ExtremeIntegerFrameResult second = SubmitExtremeIntegerFrame(&slider_value, &drag_value);
    CHECK(!second.slider_changed);
    CHECK(!second.drag_changed);
    CHECK(slider_value == expected_slider);
    CHECK(drag_value == expected_drag);

    og::DestroyContext(integer_context);
    og::SetCurrentContext(primary);
    CHECK(og::GetCurrentContext() == primary);
}

struct DragFrameResult {
    og::Vec4 source_rect;
    og::Vec4 target_rect;
    bool source_started;
    bool payload_preview;
    bool payload_delivered;
    bool delivery_event;
    int delivered_value;

    DragFrameResult()
        : source_rect(), target_rect(), source_started(false), payload_preview(false),
          payload_delivered(false), delivery_event(false), delivered_value(-1) {}
};

static DragFrameResult SubmitDragDropFrame(int source_value) {
    DragFrameResult result;
    og::NewFrame();
    const bool visible = BeginTestWindow(
        "Tests / Drag drop",
        og::Vec2(40.0f, 40.0f),
        og::Vec2(420.0f, 280.0f));
    if (visible) {
        og::Selectable("Drag source", false, og::Vec2(-1.0f, 34.0f));
        og::Vec2 minimum = og::GetItemRectMin();
        og::Vec2 maximum = og::GetItemRectMax();
        result.source_rect = og::Vec4(minimum.x, minimum.y, maximum.x, maximum.y);
        if (og::BeginDragDropSource()) {
            result.source_started = og::SetDragDropPayload(
                "OSGUI_TEST_VALUE", &source_value, (int)sizeof(source_value));
            og::EndDragDropSource();
        }

        og::Selectable("Drop target", false, og::Vec2(-1.0f, 44.0f));
        minimum = og::GetItemRectMin();
        maximum = og::GetItemRectMax();
        result.target_rect = og::Vec4(minimum.x, minimum.y, maximum.x, maximum.y);
        if (og::BeginDragDropTarget()) {
            const og::Payload* payload = og::AcceptDragDropPayload("OSGUI_TEST_VALUE");
            if (payload) {
                result.payload_preview = payload->preview;
                result.payload_delivered = payload->delivery;
                if (payload->delivery && payload->data_size == (int)sizeof(result.delivered_value))
                    memcpy(&result.delivered_value, payload->data, sizeof(result.delivered_value));
            }
            og::EndDragDropTarget();
        }
    }
    og::End();
    og::Render();

    const std::vector<og::Event>& events = og::GetEvents();
    for (size_t event_index = 0; event_index < events.size(); ++event_index) {
        if (events[event_index].type == og::Event_DragDropDelivered)
            result.delivery_event = true;
    }
    return result;
}

static og::Vec2 RectCenter(const og::Vec4& rect) {
    return og::Vec2((rect.x + rect.z) * 0.5f, (rect.y + rect.w) * 0.5f);
}

static void TestDeterministicDragDrop() {
    og::IO& io = og::GetIO();
    io.mouse_pos = og::Vec2(-1000.0f, -1000.0f);
    const DragFrameResult initial = SubmitDragDropFrame(73);

    const og::Vec2 source_center = RectCenter(initial.source_rect);
    io.AddMousePosEvent(source_center.x, source_center.y);
    io.AddMouseButtonEvent(0, true);
    int source_value = 73;
    const DragFrameResult pressed = SubmitDragDropFrame(source_value);
    CHECK(!pressed.source_started);

    const og::Vec2 target_center = RectCenter(initial.target_rect);
    io.AddMousePosEvent(target_center.x, target_center.y);
    const DragFrameResult dragging = SubmitDragDropFrame(source_value);
    CHECK(dragging.source_started);
    CHECK(dragging.payload_preview);
    CHECK(!dragging.payload_delivered);
    CHECK(og::GetContext().drag_drop_active);

    source_value = 11;
    io.AddMouseButtonEvent(0, false);
    const DragFrameResult delivered = SubmitDragDropFrame(source_value);
    CHECK(delivered.payload_preview);
    CHECK(delivered.payload_delivered);
    CHECK(delivered.delivered_value == 73);
    CHECK(delivered.delivery_event);
    CHECK(!og::GetContext().drag_drop_active);
}

static bool HasFullscreenQuadWithColor(og::U32 color, const og::Vec2& display_size) {
    const og::DrawData* draw_data = og::GetDrawData();
    for (size_t list_index = 0; list_index < draw_data->lists.size(); ++list_index) {
        const std::vector<og::DrawVert>& vertices = draw_data->lists[list_index]->vtx;
        for (size_t vertex_index = 0; vertex_index + 3 < vertices.size(); ++vertex_index) {
            const og::DrawVert& top_left = vertices[vertex_index];
            const og::DrawVert& top_right = vertices[vertex_index + 1];
            const og::DrawVert& bottom_right = vertices[vertex_index + 2];
            const og::DrawVert& bottom_left = vertices[vertex_index + 3];
            if (top_left.col != color || top_right.col != color ||
                bottom_right.col != color || bottom_left.col != color) {
                continue;
            }

            if (NearlyEqual(top_left.pos.x, 0.0f) &&
                NearlyEqual(top_left.pos.y, 0.0f) &&
                NearlyEqual(top_right.pos.x, display_size.x) &&
                NearlyEqual(top_right.pos.y, 0.0f) &&
                NearlyEqual(bottom_right.pos.x, display_size.x) &&
                NearlyEqual(bottom_right.pos.y, display_size.y) &&
                NearlyEqual(bottom_left.pos.x, 0.0f) &&
                NearlyEqual(bottom_left.pos.y, display_size.y)) {
                return true;
            }
        }
    }
    return false;
}

struct ModalFrameResult {
    og::Vec4 background_button_rect;
    bool background_pressed;
    bool background_hovered;
    bool modal_visible;
    bool dim_quad_visible;

    ModalFrameResult()
        : background_button_rect(), background_pressed(false), background_hovered(false),
          modal_visible(false), dim_quad_visible(false) {}
};

static ModalFrameResult SubmitModalFrame(bool open_popup, bool* modal_open) {
    ModalFrameResult result;
    og::NewFrame();
    const bool visible = BeginTestWindow(
        "Tests / Modal background",
        og::Vec2(20.0f, 20.0f),
        og::Vec2(260.0f, 190.0f));
    if (visible) {
        result.background_pressed = og::Button(
            "Background action",
            og::Vec2(180.0f, 36.0f));
        result.background_hovered = og::IsItemHovered();
        const og::Vec2 minimum = og::GetItemRectMin();
        const og::Vec2 maximum = og::GetItemRectMax();
        result.background_button_rect = og::Vec4(
            minimum.x,
            minimum.y,
            maximum.x,
            maximum.y);

        if (open_popup)
            og::OpenPopup("Blocking modal");
        if (og::BeginPopupModal("Blocking modal", modal_open)) {
            result.modal_visible = true;
            og::Text("Background pointer input is blocked while this modal is active.");
            og::EndPopup();
        }
    }
    og::End();
    CHECK(og::ValidateState());
    og::Render();

    result.dim_quad_visible = HasFullscreenQuadWithColor(
        og::GetStyle().colors[og::Col_ModalDim],
        og::GetIO().display_size);
    return result;
}

static void TestModalDimAndBlocking(og::Context* primary) {
    og::Context* modal_context = og::CreateContext();
    CHECK(modal_context != 0);
    if (!modal_context) {
        og::SetCurrentContext(primary);
        return;
    }

    InstallTestFont();
    ConfigureContext(og::Vec2(960.0f, 640.0f));
    bool modal_open = true;

    const ModalFrameResult opened = SubmitModalFrame(true, &modal_open);
    CHECK(opened.modal_visible);
    CHECK(opened.dim_quad_visible);
    CHECK(og::GetContext().modal_window != 0);

    og::IO& io = og::GetIO();
    const og::Vec2 button_center = RectCenter(opened.background_button_rect);
    io.AddMousePosEvent(button_center.x, button_center.y);
    io.AddMouseButtonEvent(0, true);
    const ModalFrameResult pressed = SubmitModalFrame(false, &modal_open);
    CHECK(pressed.modal_visible);
    CHECK(pressed.dim_quad_visible);
    CHECK(!pressed.background_hovered);
    CHECK(!pressed.background_pressed);
    CHECK(io.want_capture_mouse);

    io.AddMouseButtonEvent(0, false);
    const ModalFrameResult released = SubmitModalFrame(false, &modal_open);
    CHECK(released.modal_visible);
    CHECK(released.dim_quad_visible);
    CHECK(!released.background_hovered);
    CHECK(!released.background_pressed);
    CHECK(og::GetContext().active_id == 0);

    og::DestroyContext(modal_context);
    og::SetCurrentContext(primary);
    CHECK(og::GetCurrentContext() == primary);
}

static void TestBroadWidgetSmoke() {
    og::IO& io = og::GetIO();
    io.display_size = og::Vec2(1600.0f, 1400.0f);
    io.mouse_pos = og::Vec2(-1000.0f, -1000.0f);

    bool check_value = true;
    int radio_value = 0;
    float slider_value = 0.45f;
    int slider_int = 4;
    float drag_value = 2.5f;
    int drag_int = 3;
    float knob_value = 0.6f;
    int combo_value = 0;
    float color[4] = {0.4f, 0.6f, 0.9f, 1.0f};
    char readonly_text[64] = "read-only UTF-8";
    const char* combo_items[] = {"First", "Second", "Third"};
    const float values[] = {2.0f, 5.0f, 3.0f, 8.0f, 6.0f};
    const og::Vec2 points[] = {
        og::Vec2(0.0f, 2.0f),
        og::Vec2(1.0f, 4.0f),
        og::Vec2(2.0f, 3.0f)
    };
    const char* pie_labels[] = {"CPU", "GPU", "Idle"};
    const float pie_values[] = {35.0f, 45.0f, 20.0f};
    const og::Candlestick candles[] = {
        og::Candlestick(2.0f, 5.0f, 1.0f, 4.0f),
        og::Candlestick(4.0f, 7.0f, 3.0f, 6.0f)
    };

    og::NewFrame();
    const bool visible = BeginTestWindow(
        "Tests / Broad widget smoke",
        og::Vec2(10.0f, 10.0f),
        og::Vec2(1180.0f, 1260.0f));
    if (visible) {
        og::TextUnformatted("Plain text");
        og::Text("Formatted %d", 2);
        og::TextDisabled("Disabled text");
        og::TextColored(og::Vec4(0.2f, 0.8f, 0.7f, 1.0f), "Colored text");
        og::TextWrapped("Wrapped text exercises the v2 text layout path inside a constrained region.");
        og::BulletText("Bullet text");

        og::Button("Button");
        og::SameLine();
        og::SmallButton("Small button");
        og::InvisibleButton("Invisible button", og::Vec2(20.0f, 12.0f));
        og::Selectable("Selectable row");
        og::Checkbox("Checkbox", &check_value);
        og::RadioButton("Radio", &radio_value, 1);
        og::SliderFloat("Slider float", &slider_value, 0.0f, 1.0f);
        og::SliderInt("Slider int", &slider_int, 0, 10);
        og::DragFloat("Drag float", &drag_value, 0.1f, -10.0f, 10.0f);
        og::DragInt("Drag int", &drag_int, 0.2f, -10, 10);
        og::KnobFloat("Knob", &knob_value, 0.0f, 1.0f);
        og::InputText("Read only", readonly_text, (int)sizeof(readonly_text),
                      og::InputTextFlags_ReadOnly);
        og::Combo("Combo", &combo_value, combo_items, 3);

        if (og::BeginTabBar("Smoke tabs")) {
            if (og::BeginTabItem("Widgets")) {
                og::Text("Tab content");
                og::EndTabItem();
            }
            if (og::BeginTabItem("Data")) {
                og::Text("Second tab");
                og::EndTabItem();
            }
            og::EndTabBar();
        }

        og::ColorEdit4("Color", color);
        og::CollapsingHeader("Collapsing section");
        if (og::TreeNode("Tree node")) {
            og::Text("Tree content");
            og::TreePop();
        }

        og::ProgressBar(0.72f, og::Vec2(-1.0f, 24.0f), "72 percent");
        og::GlassCard("Glass card", og::Vec2(-1.0f, 54.0f), 6.0f);
        og::StatusBadge("Healthy", og::GetColorU32(og::Col_Success), true);
        og::Spinner("Loading", 8.0f, 2.0f, og::GetColorU32(og::Col_Info));
        og::Skeleton(og::Vec2(-1.0f, 14.0f));
        og::MetricCard("Metric", "42", "smoke value", og::GetColorU32(og::Col_Info));

        if (og::BeginGrid("Smoke grid", 2, 10.0f)) {
            og::Text("Grid A");
            og::NextGridColumn();
            og::Text("Grid B");
            og::EndGrid();
        }

        if (og::BeginTable("Smoke table", 2, og::TableFlags_Borders | og::TableFlags_RowBg)) {
            og::TableHeader("Name");
            og::TableHeader("Value");
            og::TableSelectable("Alpha");
            og::TableSelectable("42");
            og::EndTable();
        }

        og::PlotLines("Line plot", values, 5);
        og::PlotHistogram("Histogram", values, 5);
        if (og::BeginChart("Smoke chart", og::Vec2(-1.0f, 180.0f))) {
            og::ChartLine("Line", values, 5);
            og::ChartBars("Bars", values, 5);
            og::ChartArea("Area", values, 5);
            og::ChartScatter("Scatter", points, 3);
            og::ChartPie("Pie", pie_values, pie_labels, 3);
            og::ChartCandlesticks("Candles", candles, 2);
            og::EndChart();
        }

        og::Markdown("## Smoke markdown\n- list\n> quote\n`inline markers`");

        static og::Vec2 node_a(20.0f, 30.0f);
        static og::Vec2 node_b(230.0f, 90.0f);
        if (og::BeginNodeEditor("Smoke nodes", og::Vec2(-1.0f, 220.0f))) {
            og::NodePin output;
            og::NodePin input;
            if (og::BeginNode(1, "Source", &node_a)) {
                output = og::NodeOutput("Out");
                og::EndNode();
            }
            if (og::BeginNode(2, "Target", &node_b)) {
                input = og::NodeInput("In");
                og::EndNode();
            }
            og::NodeLink(output, input);
            og::EndNodeEditor();
        }

        og::OpenPopup("Smoke popup");
        if (og::BeginPopup("Smoke popup")) {
            og::Text("Popup content");
            og::CloseCurrentPopup();
            og::EndPopup();
        }
    }
    og::End();

    og::AddToast("Smoke notification", og::Toast_Info, 1.0f);
    og::RenderNotifications();
    CHECK(og::ValidateState());
    og::Render();

    CHECK(og::GetDrawData()->total_vtx > 0);
    CHECK(og::GetDrawData()->total_idx > 0);
    CHECK(og::GetFrameMetrics().items_submitted > 20);
    CHECK(og::GetFrameMetrics().active_windows >= 1);
}

int main() {
    og::Context* primary = og::CreateContext();
    CHECK(primary != 0);
    if (!primary) {
        printf("OSGui tests could not create a context.\n");
        return 1;
    }

    InstallTestFont();
    ConfigureContext();

    TestVersionAndLayout();
    TestNestedClipIntersectionAndCompaction();
    TestMultipleContexts(primary);
    TestLosslessQueuedInputPulses(primary);
    TestCompleteShiftTabChord(primary);
    TestIDScopesAndConflictMetrics();
    TestStyleDisabledAndItemQueries();
    TestClosedWindowContract();
    TestChildScrollingAndListClipper();
    TestMemoryAndFilePersistence();
    TestPointerWidthTextureAndImageCommand();
    TestQueuedUTF8AndBackspace(primary);
    TestOrderedTextActionsAndCompleteCtrlZChord(primary);
    TestInputTextSelectionHistoryAndWords(primary);
    TestNavigationActivationIsConsumed(primary);
    TestQueuedSliderPulseCoordinates(primary);
    TestPassiveExtremeIntegerStability(primary);
    TestDeterministicDragDrop();
    TestModalDimAndBlocking(primary);
    TestBroadWidgetSmoke();

    og::DestroyContext(primary);
    CHECK(og::GetCurrentContext() == 0);

    if (g_tests.failures == 0) {
        printf("OSGui v2 tests passed: %d checks.\n", g_tests.checks);
        return 0;
    }

    printf("OSGui v2 tests failed: %d of %d checks.\n",
           g_tests.failures, g_tests.checks);
    return 1;
}
