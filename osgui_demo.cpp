// OSGui dashboard showcase.
#include "osgui.h"
#include <math.h>

namespace og {

void ShowDemoWindow(bool* p_open) {
    SetNextWindowPos(Vec2(24, 24));
    SetNextWindowSize(Vec2(875, 815));
    if (!Begin("OSGui / Studio Dashboard", p_open)) { End(); return; }

    static bool live_preview = true;
    static bool notifications = false;
    static bool adaptive = true;
    static int quality = 0;
    static float opacity = 0.76f;
    static int intensity = 68;
    static char project_name[96] = "Telemetry workspace";
    static char notes[256] = "UTF-8 ready: cafe, Omega, Arabic, Japanese and emoji: cafe \xCE\xA9 \xD9\x85\xD8\xB1\xD8\xAD\xD8\xA8\xD8\xA7 \xE6\x97\xA5\xE6\x9C\xAC\xE8\xAA\x9E \xF0\x9F\x98\x80";
    static int renderer = 1;
    static float accent[4] = { 0.45f, 0.35f, 1.0f, 1.0f };

    TextColored(Vec4(0.55f, 0.93f, 0.85f, 1.0f), "OSGUI STUDIO");
    Text("Modern native interfaces without a retained widget tree.");
    SameLine(0, 18);
    TextDisabled("%.0f FPS", GetIO().framerate);

    if (Button("Dark", Vec2(78, 32))) {
        SetTheme(Theme_Dark, 0.35f);
    }
    SameLine();
    if (Button("Light", Vec2(78, 32))) {
        SetTheme(Theme_Light, 0.35f);
    }
    SameLine();
    TextDisabled(GetContext().theme_preset == Theme_Light ? "Light theme active" : "Dark theme active");

    Separator();

    GlassCard("GPU glass card / live framebuffer blur + shader tint", Vec2(-1, 54), 6.0f);

    if (BeginTabBar("studio-sections")) {
        if (BeginTabItem("Overview")) {
            TextDisabled("EDITABLE UTF-8 FORM");
            InputText("Project", project_name, (int)sizeof(project_name));
            InputTextMultiline("Notes", notes, (int)sizeof(notes), Vec2(-1, 62));
            const char* renderers[] = { "DirectX 11", "OpenGL 3", "OpenGL compatibility" };
            Combo("Renderer", &renderer, renderers, 3);
            ColorEdit4("Accent colour", accent);
            if (Button("Show notification")) AddToast("Workspace saved successfully", Toast_Success);
            SameLine();
            if (Button("Open quick actions")) OpenPopup("Quick actions");
            if (BeginPopup("Quick actions")) {
                Text("Quick actions"); Separator();
                if (Button("Apply and close", Vec2(-1, 32))) CloseCurrentPopup();
                if (Button("Cancel", Vec2(-1, 32))) CloseCurrentPopup();
                EndPopup();
            }
        }
        if (BeginTabItem("Data table")) {
            TextDisabled("SORTABLE + SELECTABLE TABLE");
            if (BeginTable("processes", 3, TableFlags_RowBg | TableFlags_Borders | TableFlags_Sortable)) {
                TableHeader("Process"); TableHeader("CPU"); TableHeader("State");
                TableSelectable("Renderer"); TableSelectable("12.4%"); TableSelectable("Running", true);
                TableSelectable("Streaming"); TableSelectable("4.8%"); TableSelectable("Ready");
                TableSelectable("Compiler"); TableSelectable("0.2%"); TableSelectable("Idle");
                EndTable();
            }
        }
        EndTabBar();
    }

    if (BeginGrid("dashboard-grid", 2, 16.0f)) {
        TextDisabled("INTERACTION + MOTION");
        Checkbox("Enable live preview", &live_preview);
        Checkbox("Desktop notifications", &notifications);
        Checkbox("Adaptive rendering", &adaptive);
        RadioButton("Balanced", &quality, 0); SameLine();
        RadioButton("Quality", &quality, 1); SameLine();
        RadioButton("Speed", &quality, 2);
        SliderFloat("Opacity", &opacity, 0.0f, 1.0f, "%.2f");
        SliderInt("Intensity", &intensity, 0, 100);
        ProgressBar(intensity / 100.0f, Vec2(-1, 30), "GPU budget");

        NextGridColumn();

        TextDisabled("NATIVE MARKDOWN");
        Markdown(
            "## Project notes\n"
            "OSGui mixes an **immediate API** with tiny retained caches.\n"
            "- Animated controls keyed by stable IDs\n"
            "- Grid and sequential layouts\n"
            "> Markdown is parsed only when you call it.\n"
            "```cpp\n"
            "if (og::Button(\"Deploy\")) Run();\n"
            "```");

        NextGridColumn();

        static StreamingSeries frame_time(360);
        static float sample_clock = 0.0f;
        static float wave_time = 0.0f;
        sample_clock += GetIO().delta_time;
        wave_time += GetIO().delta_time;
        if (sample_clock >= 1.0f / 60.0f) {
            sample_clock = 0.0f;
            float sample = 8.2f + sinf(wave_time * 2.2f) * 1.7f + sinf(wave_time * 6.1f) * 0.55f;
            frame_time.Push(sample);
        }

        TextDisabled("REAL-TIME STREAM");
        if (BeginChart("Frame time / ms", Vec2(-1, 158))) {
            ChartLine("frame", frame_time);
            EndChart();
        }

        NextGridColumn();

        static float usage[] = { 42, 73, 58, 86, 64, 77, 51, 92, 69, 82, 61, 75 };
        static float target[] = { 62, 62, 62, 62, 62, 62, 62, 62, 62, 62, 62, 62 };
        TextDisabled("MULTI-SERIES CHART");
        if (BeginChart("Subsystem load / %", Vec2(-1, 158))) {
            ChartBars("usage", usage, 12);
            ChartLine("target", target, 12, GetColorU32(Col_Warning));
            EndChart();
        }

        EndGrid();
    }

    Separator();
    if (Button("Run task", Vec2(122, 34))) {}
    SameLine();
    if (Button("Save preset", Vec2(122, 34))) {}
    SameLine();
    const std::vector<Event>& events = GetEvents();
    TextDisabled("%d frame event%s  /  %d animated states",
                 (int)events.size(), events.size() == 1 ? "" : "s",
                 (int)GetContext().animations.size());

    End();
}

void ShowNodeEditorDemo(bool* p_open) {
    SetNextWindowPos(Vec2(920, 24));
    SetNextWindowSize(Vec2(485, 400));
    if (!Begin("OSGui / Node Lab", p_open)) { End(); return; }

    TextColored(Vec4(0.55f, 0.93f, 0.85f, 1.0f), "INTERACTIVE GRAPH");
    TextDisabled("Drag a node by its title bar. Links follow every frame.");

    static Vec2 source_pos(24, 38);
    static Vec2 process_pos(240, 126);
    NodePin samples, stream, average;
    if (BeginNodeEditor("telemetry-graph", Vec2(-1, 290))) {
        if (BeginNode(1, "Telemetry", &source_pos, Vec2(180, 118))) {
            NodeInput("Clock");
            samples = NodeOutput("Samples");
            EndNode();
        }
        if (BeginNode(2, "Smoothing", &process_pos, Vec2(190, 126))) {
            stream = NodeInput("Stream");
            average = NodeOutput("Average");
            EndNode();
        }
        NodeLink(samples, stream);
        (void)average;
        EndNodeEditor();
    }
    End();
}

} // namespace og
