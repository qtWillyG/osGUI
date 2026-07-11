// osgui_demo.cpp - the demo windows (cf. imgui_demo.cpp)
#include "osgui.h"

namespace og {

void ShowDemoWindow(bool* p_open) {
    SetNextWindowPos(Vec2(30, 30));
    SetNextWindowSize(Vec2(470, 540));
    if (!Begin("osGUI Demo", p_open)) { End(); return; }

    Text("osGUI says hello! (v1.0.0)");
    TextDisabled("a tiny Dear ImGui-style library in C++");
    Separator();

    static bool cfg_nav = true, cfg_dock = false, cfg_view = false;
    if (CollapsingHeader("Configuration")) {
        Checkbox("io.ConfigFlags: NavEnableKeyboard", &cfg_nav);
        Checkbox("io.ConfigFlags: DockingEnable",     &cfg_dock);
        Checkbox("io.ConfigFlags: ViewportsEnable",   &cfg_view);
    }

    if (CollapsingHeader("Widgets")) {
        static int counter = 0;
        static bool check = true;
        static int radio = 0;
        static float f1 = 0.345f;
        static int   i1 = 42;

        TextDisabled("Basic");
        if (Button("Button")) counter++;
        SameLine();
        Text("clicked %d times", counter);

        Checkbox("checkbox", &check);

        RadioButton("radio a", &radio, 0); SameLine();
        RadioButton("radio b", &radio, 1); SameLine();
        RadioButton("radio c", &radio, 2);

        TextDisabled("Sliders");
        SliderFloat("float", &f1, 0.0f, 1.0f);
        SliderInt("int", &i1, 0, 100);

        TextDisabled("Trees");
        if (TreeNode("Root node")) {
            BulletText("child leaf 0");
            if (TreeNode("Child node")) {
                BulletText("grand-child 0");
                BulletText("grand-child 1");
                TreePop();
            }
            BulletText("child leaf 1");
            TreePop();
        }
    }

    if (CollapsingHeader("Progress / Plots")) {
        static float t = 0.0f;
        t += GetIO().delta_time * 0.3f;
        if (t > 1.0f) t -= 1.0f;
        ProgressBar(t, Vec2(-1, 0), 0);
        SameLine(); Text("Loading...");

        static float samples[90];
        static int   so = 0;
        static float acc = 0;
        acc += GetIO().delta_time;
        if (acc > 1.0f / 60.0f) { acc = 0; samples[so] = GetIO().delta_time * 1000.0f; so = (so + 1) % 90; }
        // rotate so newest is last
        float ordered[90];
        for (int i = 0; i < 90; i++) ordered[i] = samples[(so + i) % 90];
        PlotLines("ms/frame", ordered, 90, Vec2(-1, 60));
        PlotHistogram("hist", ordered, 90, Vec2(-1, 60));
    }

    End();
}

} // namespace og
