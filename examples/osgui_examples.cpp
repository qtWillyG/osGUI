#include "osgui.h"
#include "osgui_examples.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

namespace og {

void ShowSettingsExample(bool* open) {
    SetNextWindowSize(Vec2(660, 590), Cond_FirstUseEver);
    if (!Begin("Example / Settings", open)) {
        End();
        return;
    }

    static bool autosave = true;
    static bool telemetry = false;
    static int language = 0;
    static int autosave_seconds = 30;
    static float notification_volume = 0.7f;
    static char profile[64] = "Developer";

    TextColored(Vec4(0.55f, 0.93f, 0.85f, 1.0f), "PRODUCT SETTINGS");
    SameLine();
    StatusBadge("Local profile", GetColorU32(Col_Info));
    TextDisabled("Scoped state, accessible disabled controls, and precision inputs.");
    Separator();

    if (BeginGrid("settings-columns", 2, 16.0f)) {
        TextDisabled("GENERAL");
        InputText("Profile", profile, (int)sizeof(profile));
        const char* languages[] = {"English", "Deutsch", "Japanese", "Spanish"};
        Combo("Language", &language, languages, 4);
        Checkbox("Automatic saves", &autosave);
        Checkbox("Anonymous telemetry", &telemetry);

        BeginDisabled(!autosave);
        DragInt("Save interval", &autosave_seconds, 0.5f, 5, 300);
        EndDisabled();

        NextGridColumn();

        TextDisabled("FEEDBACK");
        KnobFloat("Volume", &notification_volume, 0.0f, 1.0f,
                  0.01f, Vec2(96, 108));
        ProgressBar(notification_volume, Vec2(-1, 24), "Notification volume");

        BeginDisabled(profile[0] == 0);
        if (Button("Save settings", Vec2(-1, 36)))
            AddToast("Settings saved", Toast_Success);
        const bool save_visible = IsItemVisible();
        EndDisabled();
        TextDisabled("Save action visible: %s", save_visible ? "yes" : "no");

        EndGrid();
    }

    End();
}

void ShowLauncherExample(bool* open) {
    SetNextWindowSize(Vec2(720, 520), Cond_FirstUseEver);
    if (!Begin("Example / Launcher", open)) {
        End();
        return;
    }

    static int channel = 0;
    static int parallel_downloads = 6;
    static bool verify_assets = true;
    static bool loading_manifest = false;

    TextColored(Vec4(0.55f, 0.93f, 0.85f, 1.0f), "OSGUI 2.0 ALPHA LAUNCHER");
    SameLine();
    StatusBadge("Services healthy", GetColorU32(Col_Success), true);
    TextDisabled("Version %s / renderer-ready workspace", GetVersion());

    GlassCard("Signed manifest / 18 packages / native backends ready", Vec2(-1, 64), 7.0f);

    const char* channels[] = {"Stable", "Preview", "Nightly"};
    Combo("Release channel", &channel, channels, 3);
    Checkbox("Verify assets before launch", &verify_assets);
    DragInt("Parallel downloads", &parallel_downloads, 0.25f, 1, 16);

    ProgressBar(0.82f, Vec2(-1, 30), "Preparing shaders");
    Spinner("Indexing package graph", 8.0f, 2.0f, GetColorU32(Col_Info));

    Checkbox("Preview loading state", &loading_manifest);
    if (loading_manifest) {
        Skeleton(Vec2(-1, 14));
        Skeleton(Vec2(-110, 14));
        Skeleton(Vec2(-220, 52), 9.0f);
    }

    BeginDisabled(loading_manifest);
    if (Button("Launch workspace", Vec2(190, 40)))
        AddToast("Workspace launched", Toast_Success);
    const bool launch_hovered = IsItemHovered();
    EndDisabled();
    if (launch_hovered)
        SetTooltip("Launch emits a frame-local click event and a toast.");

    End();
}

void ShowFileBrowserExample(bool* open) {
    SetNextWindowSize(Vec2(760, 610), Cond_FirstUseEver);
    if (!Begin("Example / Virtual File Browser", open)) {
        End();
        return;
    }

    static char filter[96] = "";
    static int selected_file = 0;
    static int pinned_file = -1;

    InputText("Filter", filter, (int)sizeof(filter));
    TextDisabled("C:/Projects/osgui-v2 / 250 synthetic entries");
    if (filter[0] != 0)
        StatusBadge("Host filter active", GetColorU32(Col_Info));

    if (BeginChild("virtual-files", Vec2(-1, 350),
                   ChildFlags_Borders | ChildFlags_AlwaysUseWindowPadding)) {
        const float row_step = 27.0f;
        ListClipper clipper;
        clipper.Begin(250, row_step);
        while (clipper.Step()) {
            for (int file_index = clipper.display_start;
                 file_index < clipper.display_end;
                 ++file_index) {
                char label[96];
                if ((file_index % 11) == 0)
                    snprintf(label, sizeof(label), "folder_%03d/", file_index);
                else
                    snprintf(label, sizeof(label), "module_%03d.cpp", file_index);

                PushID(file_index);
                if (Selectable(label, selected_file == file_index,
                               Vec2(-1, row_step - GetStyle().item_spacing.y))) {
                    selected_file = file_index;
                }
                if (BeginDragDropSource()) {
                    SetDragDropPayload("OSGUI_FILE_INDEX", &file_index, (int)sizeof(file_index));
                    Text("Pin %s", label);
                    EndDragDropSource();
                }
                PopID();
            }
        }
        clipper.End();
    }
    EndChild();

    Button("Drop a row here to pin it", Vec2(-1, 42));
    if (BeginDragDropTarget()) {
        const Payload* payload = AcceptDragDropPayload("OSGUI_FILE_INDEX");
        if (payload && payload->delivery && payload->data_size == (int)sizeof(pinned_file)) {
            memcpy(&pinned_file, payload->data, sizeof(pinned_file));
            AddToast("File pinned", Toast_Success);
        }
        EndDragDropTarget();
    }

    Text("Selected row: %d", selected_file);
    if (pinned_file >= 0)
        StatusBadge("Pinned to workspace", GetColorU32(Col_Success));

    End();
}

void ShowPerformanceMonitorExample(bool* open) {
    SetNextWindowSize(Vec2(800, 650), Cond_FirstUseEver);
    if (!Begin("Example / Performance Monitor", open)) {
        End();
        return;
    }

    static StreamingSeries cpu(240);
    static StreamingSeries gpu(240);
    static float time = 0.0f;
    static float refresh_rate = 60.0f;
    time += GetIO().delta_time;
    cpu.Push(42.0f + sinf(time * 2.1f) * 18.0f);
    gpu.Push(58.0f + sinf(time * 1.4f + 1.0f) * 14.0f);

    TextColored(Vec4(0.55f, 0.93f, 0.85f, 1.0f), "LIVE PERFORMANCE");
    SameLine();
    StatusBadge("Sampling", GetColorU32(Col_Success), true);
    SameLine();
    Spinner("", 7.0f, 2.0f, GetColorU32(Col_Info));

    if (BeginGrid("performance-cards", 3, 12.0f)) {
        char value[32];
        snprintf(value, sizeof(value), "%.0f", GetIO().framerate);
        MetricCard("Frame rate", value, "frames / second", GetColorU32(Col_Success));
        NextGridColumn();
        MetricCard("CPU", "42%", "rolling average", GetColorU32(Col_Info));
        NextGridColumn();
        MetricCard("GPU", "58%", "rolling average", GetColorU32(Col_Warning));
        EndGrid();
    }

    DragFloat("Sample rate", &refresh_rate, 0.5f, 10.0f, 240.0f, "%.0f Hz");

    if (BeginChart("CPU and GPU / percent", Vec2(-1, 210))) {
        ChartLine("CPU", cpu, GetColorU32(Col_Info));
        ChartLine("GPU", gpu, GetColorU32(Col_Warning));
        EndChart();
    }

    const Candlestick candles[] = {
        Candlestick(32, 48, 28, 43),
        Candlestick(43, 51, 39, 41),
        Candlestick(41, 56, 40, 53),
        Candlestick(53, 57, 45, 47),
        Candlestick(47, 61, 46, 59)
    };
    if (BeginChart("Frame ranges", Vec2(-1, 180))) {
        ChartCandlesticks("Frame", candles, 5);
        EndChart();
    }

    End();
}

} // namespace og
