// OSGui dashboard showcase.
#include "osgui.h"
#include <math.h>

namespace og {

void ShowDemoWindow(bool* p_open) {
    SetNextWindowPos(Vec2(36, 32));
    SetNextWindowSize(Vec2(910, 740));
    if (!Begin("OSGui / Studio Dashboard", p_open)) { End(); return; }

    static bool light_theme = false;
    static bool live_preview = true;
    static bool notifications = false;
    static bool adaptive = true;
    static int quality = 0;
    static float opacity = 0.76f;
    static int intensity = 68;

    TextColored(Vec4(0.55f, 0.93f, 0.85f, 1.0f), "OSGUI STUDIO");
    Text("Modern native interfaces without a retained widget tree.");
    SameLine(0, 18);
    TextDisabled("%.0f FPS", GetIO().framerate);

    if (Button("Dark", Vec2(78, 32))) {
        light_theme = false;
        SetTheme(Theme_Dark, 0.35f);
    }
    SameLine();
    if (Button("Light", Vec2(78, 32))) {
        light_theme = true;
        SetTheme(Theme_Light, 0.35f);
    }
    SameLine();
    TextDisabled(light_theme ? "Light theme active" : "Dark theme active");

    Separator();

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

} // namespace og
