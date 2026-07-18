// OSGui v2 dashboard showcase.
#include "osgui.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

namespace og {

static void ShowVirtualActivityStream() {
    static int selected_row = -1;

    TextDisabled("VIRTUALIZED ACTIVITY STREAM / 500 ROWS");
    if (BeginChild("activity-stream", Vec2(-1, 190),
                   ChildFlags_Borders | ChildFlags_AlwaysUseWindowPadding)) {
        const float row_step = 25.0f;
        ListClipper clipper;
        clipper.Begin(500, row_step);
        while (clipper.Step()) {
            for (int row = clipper.display_start; row < clipper.display_end; ++row) {
                char label[96];
                const char* subsystem = (row % 3 == 0) ? "renderer" :
                                        (row % 3 == 1) ? "telemetry" : "compiler";
                snprintf(label, sizeof(label), "%03d  %-10s  frame packet accepted", row, subsystem);

                PushID(row);
                if (Selectable(label, selected_row == row, Vec2(-1, row_step - GetStyle().item_spacing.y)))
                    selected_row = row;
                if (IsItemHovered())
                    SetTooltip("Rows outside the child clip rectangle are never submitted.");
                PopID();
            }
        }
        clipper.End();
    }
    EndChild();

    if (selected_row >= 0)
        TextDisabled("Selected virtual row: %d", selected_row);
}

static void ShowAssetTransferLab() {
    static int installed_asset = -1;
    const char* assets[] = {
        "Telemetry schema",
        "Midnight palette",
        "GPU capture",
        "Build manifest"
    };

    TextWrapped("Drag any package into the deployment target. Payload bytes are copied by OSGui, "
                "so the source data does not need to remain alive during the gesture.");

    if (BeginChild("asset-source-list", Vec2(-1, 142), ChildFlags_Borders)) {
        for (int asset = 0; asset < 4; ++asset) {
            PushID(asset);
            Selectable(assets[asset], installed_asset == asset);
            if (BeginDragDropSource()) {
                SetDragDropPayload("OSGUI_ASSET_INDEX", &asset, (int)sizeof(asset));
                Text("Deploy %s", assets[asset]);
                EndDragDropSource();
            }
            PopID();
        }
    }
    EndChild();

    Button("Drop package here", Vec2(-1, 44));
    if (BeginDragDropTarget()) {
        const Payload* payload = AcceptDragDropPayload("OSGUI_ASSET_INDEX");
        if (payload && payload->delivery && payload->data_size == (int)sizeof(installed_asset)) {
            memcpy(&installed_asset, payload->data, sizeof(installed_asset));
            AddToast("Package deployed from a typed drag payload", Toast_Success);
        }
        EndDragDropTarget();
    }

    if (installed_asset >= 0 && installed_asset < 4)
        StatusBadge(assets[installed_asset], GetColorU32(Col_Success), true);
    else
        StatusBadge("Waiting for a package", GetColorU32(Col_Info));
}

void ShowDemoWindow(bool* p_open) {
    SetNextWindowPos(Vec2(24, 24), Cond_FirstUseEver);
    SetNextWindowSize(Vec2(875, 840), Cond_FirstUseEver);
    SetNextWindowSizeConstraints(Vec2(680, 520), Vec2(1180, 1200));
    if (!Begin("OSGui / Studio Dashboard", p_open)) {
        End();
        return;
    }

    static bool live_preview = true;
    static bool notifications = false;
    static bool adaptive = true;
    static bool simulate_loading = false;
    static int quality = 0;
    static int worker_count = 12;
    static float opacity = 0.76f;
    static float exposure = 0.45f;
    static float cpu_budget = 8.3f;
    static char project_name[96] = "Telemetry workspace";
    static char notes[320] =
        "UTF-8 input: cafe, Omega, Arabic, Japanese and emoji: "
        "cafe \xCE\xA9 \xD9\x85\xD8\xB1\xD8\xAD\xD8\xA8\xD8\xA7 "
        "\xE6\x97\xA5\xE6\x9C\xAC\xE8\xAA\x9E \xF0\x9F\x98\x80";
    static int renderer = 1;
    static float accent[4] = {0.45f, 0.35f, 1.0f, 1.0f};

    TextColored(Vec4(0.55f, 0.93f, 0.85f, 1.0f), "OSGUI 2.0 ALPHA");
    SameLine(0.0f, 14.0f);
    TextDisabled("%s", GetVersion());
    SameLine(0.0f, 18.0f);
    StatusBadge("Core online", GetColorU32(Col_Success), true);
    SameLine(0.0f, 10.0f);
    Spinner("Streaming", 7.0f, 2.0f, GetColorU32(Col_Info));

    Text("Instrument-grade immediate UI without a retained widget tree.");

    if (Button("Midnight", Vec2(92, 32)))
        SetTheme(Theme_Dark, 0.35f);
    SameLine();
    if (Button("Daylight", Vec2(92, 32)))
        SetTheme(Theme_Light, 0.35f);
    SameLine();
    if (Button("High contrast", Vec2(118, 32)))
        SetTheme(Theme_HighContrast, 0.2f);
    SameLine();
    bool reduced_motion = IsReducedMotion();
    if (Checkbox("Reduced motion", &reduced_motion))
        SetReducedMotion(reduced_motion);

    Separator();
    GlassCard("Renderer-neutral draw data / compact commands / pointer-width textures",
              Vec2(-1, 54), 6.0f);

    if (BeginTabBar("studio-sections")) {
        if (BeginTabItem("Command center")) {
            if (BeginGrid("headline-metrics", 3, 12.0f)) {
                MetricCard("Frame time", "8.3 ms", "120 Hz target", GetColorU32(Col_Success));
                NextGridColumn();
                MetricCard("Draw commands", "46", "after compaction", GetColorU32(Col_Info));
                NextGridColumn();
                MetricCard("Virtual rows", "500", "8 submitted", GetColorU32(Col_Warning));
                EndGrid();
            }

            if (BeginGrid("command-grid", 2, 16.0f)) {
                TextDisabled("PROJECT + INPUT");
                InputText("Project", project_name, (int)sizeof(project_name));
                InputTextMultiline("Notes", notes, (int)sizeof(notes), Vec2(-1, 74));
                const char* renderers[] = {
                    "DirectX 11",
                    "OpenGL 3",
                    "OpenGL compatibility"
                };
                Combo("Renderer", &renderer, renderers, 3);
                ColorEdit4("Accent colour", accent);

                NextGridColumn();

                TextDisabled("PRECISION CONTROLS");
                DragFloat("CPU budget / ms", &cpu_budget, 0.05f, 1.0f, 33.0f, "%.2f ms");
                DragInt("Worker threads", &worker_count, 0.2f, 1, 64);
                SliderFloat("Window opacity", &opacity, 0.2f, 1.0f, "%.2f");
                KnobFloat("Exposure", &exposure, 0.0f, 1.0f, 0.008f, Vec2(86, 96));
                EndGrid();
            }

            Checkbox("Enable live preview", &live_preview);
            SameLine();
            Checkbox("Desktop notifications", &notifications);
            SameLine();
            Checkbox("Adaptive rendering", &adaptive);

            BeginDisabled(!live_preview);
            if (Button("Apply live patch", Vec2(150, 34)))
                AddToast("Live patch queued", Toast_Info);
            const bool disabled_item_visible = IsItemVisible();
            const Vec2 disabled_item_size = GetItemRectSize();
            EndDisabled();
            SameLine();
            TextDisabled("disabled scope / visible=%s / %.0fx%.0f",
                         disabled_item_visible ? "yes" : "no",
                         disabled_item_size.x,
                         disabled_item_size.y);

            if (Button("Inspect this item", Vec2(150, 34)))
                AddToast("The frame event queue recorded the click", Toast_Success);
            const bool item_hovered = IsItemHovered();
            const bool item_focused = IsItemFocused();
            const ID item_id = GetItemID();
            if (item_hovered)
                SetTooltip("Item queries are valid immediately after widget submission.");
            SameLine();
            TextDisabled("id=%llu / hovered=%s / focused=%s",
                         (unsigned long long)item_id,
                         item_hovered ? "yes" : "no",
                         item_focused ? "yes" : "no");

            Checkbox("Simulate loading content", &simulate_loading);
            if (simulate_loading) {
                Skeleton(Vec2(-1, 16));
                Skeleton(Vec2(-120, 16));
                Skeleton(Vec2(-250, 44), 10.0f);
            }

            if (Button("Open quick actions"))
                OpenPopup("Quick actions");
            if (BeginPopup("Quick actions")) {
                Text("Workspace actions");
                Separator();
                if (Button("Save snapshot", Vec2(-1, 32))) {
                    AddToast("In-memory snapshot created", Toast_Success);
                    CloseCurrentPopup();
                }
                if (Button("Cancel", Vec2(-1, 32)))
                    CloseCurrentPopup();
                EndPopup();
            }

            EndTabItem();
        }

        if (BeginTabItem("Telemetry")) {
            static StreamingSeries frame_time(360);
            static float sample_clock = 0.0f;
            static float wave_time = 0.0f;
            sample_clock += GetIO().delta_time;
            wave_time += GetIO().delta_time;
            if (sample_clock >= 1.0f / 60.0f) {
                sample_clock = 0.0f;
                const float sample = 8.2f + sinf(wave_time * 2.2f) * 1.7f +
                                     sinf(wave_time * 6.1f) * 0.55f;
                frame_time.Push(sample);
            }

            if (BeginChart("Frame time / ms", Vec2(-1, 170))) {
                ChartLine("frame", frame_time, GetColorU32(Col_Info));
                EndChart();
            }

            ShowVirtualActivityStream();
            EndTabItem();
        }

        if (BeginTabItem("Transfer lab")) {
            ShowAssetTransferLab();
            EndTabItem();
        }

        if (BeginTabItem("Design notes")) {
            Markdown(
                "## OSGui v2 identity\n"
                "- Immediate frame-driven authoring\n"
                "- Explicit `PushID()` scopes for repeated rows\n"
                "- Child scrolling with clipped submission\n"
                "> Accessibility is a runtime behavior, not a separate skin.\n"
                "```cpp\n"
                "og::BeginDisabled(!can_deploy);\n"
                "og::Button(\"Deploy\");\n"
                "og::EndDisabled();\n"
                "```");
            EndTabItem();
        }

        EndTabBar();
    }

    Separator();
    RadioButton("Balanced", &quality, 0);
    SameLine();
    RadioButton("Quality", &quality, 1);
    SameLine();
    RadioButton("Speed", &quality, 2);
    SameLine(0.0f, 20.0f);
    const FrameMetrics& metrics = GetFrameMetrics();
    TextDisabled("%d items / %d clipped / %d ID conflicts / %.0f FPS",
                 metrics.items_submitted,
                 metrics.clipped_items,
                 metrics.id_conflicts,
                 GetIO().framerate);

    End();
}

void ShowNodeEditorDemo(bool* p_open) {
    SetNextWindowPos(Vec2(920, 24), Cond_FirstUseEver);
    SetNextWindowSize(Vec2(485, 400), Cond_FirstUseEver);
    if (!Begin("OSGui / Node Lab", p_open)) {
        End();
        return;
    }

    TextColored(Vec4(0.55f, 0.93f, 0.85f, 1.0f), "INTERACTIVE GRAPH");
    SameLine();
    StatusBadge("Live", GetColorU32(Col_Success), true);
    TextDisabled("Drag a node by its title bar. Links follow every frame.");

    static Vec2 source_pos(24, 38);
    static Vec2 process_pos(240, 126);
    NodePin samples;
    NodePin stream;
    NodePin average;

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
