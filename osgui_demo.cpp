// OSGui control-center showcase.
#include "osgui.h"

namespace og {

void ShowDemoWindow(bool* p_open) {
    SetNextWindowPos(Vec2(42, 34));
    SetNextWindowSize(Vec2(560, 660));
    if (!Begin("OSGui / Control Center", p_open)) { End(); return; }

    TextColored(Vec4(0.55f, 0.93f, 0.85f, 1.0f), "WELCOME BACK");
    Text("A modern immediate-mode interface for native tools.");
    TextDisabled("Lightweight core  /  zero external dependencies  /  60 FPS");
    Separator();

    static bool enabled = true, notifications = false;
    TextDisabled("PREFERENCES");
    Checkbox("Enable live preview", &enabled);
    Checkbox("Desktop notifications", &notifications);

    TextDisabled("QUICK ACTIONS");
    static int counter = 0;
    if (Button("Run task", Vec2(122, 34))) counter++;
    SameLine();
    if (Button("Save preset", Vec2(122, 34))) counter++;
    SameLine(); TextDisabled("%d actions", counter);

    Separator();
    TextDisabled("CONTROLS");
    {
        static int counter = 0;
        static bool check = true;
        static int radio = 0;
        static float f1 = 0.345f;
        static int   i1 = 42;

        Checkbox("Adaptive mode", &check);
        RadioButton("Balanced", &radio, 0); SameLine();
        RadioButton("Quality", &radio, 1); SameLine();
        RadioButton("Speed", &radio, 2);
        SliderFloat("Opacity", &f1, 0.0f, 1.0f, "%.2f");
        SliderInt("Intensity", &i1, 0, 100);
    }

    Separator();
    TextDisabled("LIVE ACTIVITY");
    {
        static float t = 0.0f;
        t += GetIO().delta_time * 0.3f;
        if (t > 1.0f) t -= 1.0f;
        ProgressBar(t, Vec2(-1, 0), 0);
        SameLine(); Text("Syncing workspace");

        static float samples[90];
        static int   so = 0;
        static float acc = 0;
        acc += GetIO().delta_time;
        if (acc > 1.0f / 60.0f) { acc = 0; samples[so] = GetIO().delta_time * 1000.0f; so = (so + 1) % 90; }
        // rotate so newest is last
        float ordered[90];
        for (int i = 0; i < 90; i++) ordered[i] = samples[(so + i) % 90];
        PlotLines("Frame time", ordered, 90, Vec2(-1, 72));
    }

    End();
}

} // namespace og
