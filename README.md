<div align="center">

# OSGui

### Modern immediate-mode UI for native C++ applications

Build animated tools, launchers, editors, overlays, node graphs, and telemetry
panels with a small frame-driven API and renderer-neutral draw data.

[![C++11](https://img.shields.io/badge/C%2B%2B-11%2B-6D5DFC?style=for-the-badge&logo=cplusplus&logoColor=white)](#requirements)
[![Windows + Linux](https://img.shields.io/badge/platform-Windows%20%7C%20Linux-171A28?style=for-the-badge)](#backend-matrix)
[![OpenGL + DirectX](https://img.shields.io/badge/renderers-OpenGL%203%20%7C%20DX11-27C7B8?style=for-the-badge)](#backend-matrix)
[![Tests](https://img.shields.io/badge/tests-passing-22C55E?style=for-the-badge)](#testing)
[![MIT](https://img.shields.io/badge/license-MIT-8B5CF6?style=for-the-badge)](LICENSE)

[Quick start](#quick-start) · [Features](#feature-overview) · [Backends](#backend-matrix) · [Examples](#examples) · [Architecture](#architecture) · [Status](#project-status)

</div>

![OSGui Studio Dashboard](docs/demo.png)

OSGui keeps the productive part of immediate-mode UI—describe the interface
each frame, receive a result immediately—but gives it a softer visual language
and several systems normally supplied by extensions. Rounded surfaces,
animation, charts, Markdown, node editing, docking helpers, persistence, and
GPU effects share one compact draw pipeline.

> OSGui is an independent project inspired by immediate-mode GUI design. It is
> not a fork of Dear ImGui and does not use Dear ImGui source code.

## Why OSGui?

<table>
<tr>
<td width="33%" valign="top">

### Designed for modern tools

Animated switches, translucent cards, gradient accents, rounded windows,
shadows, theme transitions, and a live Theme Studio are included by default.

</td>
<td width="33%" valign="top">

### More than basic widgets

Real-time charts, sortable tables, rich text, editable UTF-8 text, popups,
notifications, node graphs, grids, tabs, and colour editing are native APIs.

</td>
<td width="33%" valign="top">

### Backend-neutral core

The core emits vertices, indices, textures, clip rectangles, and effect
commands. Platform and graphics integrations remain separate and replaceable.

</td>
</tr>
</table>

## Quick start

### Build the Windows showcase

```bat
git clone <your-osgui-repository>
cd osgui
build.bat
osgui_demo.exe
```

`build.bat` uses an active Visual Studio developer shell or locates the newest
installed MSVC toolchain through `vswhere`.

For reusable CMake targets, tests, OpenGL 3, and DirectX 11:

```bat
build-cmake.bat
ctest --test-dir build -C Release --output-on-failure
```

### Frame integration

```cpp
og::CreateContext();
OG_ImplWin32_Init(hwnd);
OG_ImplOpenGL2_Init(); // OpenGL compatibility demo backend

while (running) {
    OG_ImplOpenGL2_NewFrame();
    OG_ImplWin32_NewFrame();
    og::NewFrame();

    if (og::Begin("Project settings")) {
        static char name[128] = "Telemetry workspace";
        static bool preview = true;
        static float opacity = 0.78f;

        og::InputText("Project", name, sizeof(name));
        og::Checkbox("Live preview", &preview);
        og::SliderFloat("Opacity", &opacity, 0.0f, 1.0f);

        if (og::Button("Save"))
            og::AddToast("Workspace saved", og::Toast_Success);
    }
    og::End();

    og::RenderNotifications();
    og::Render();
    OG_ImplOpenGL2_RenderDrawData(og::GetDrawData());
}
```

## Feature overview

| Area | Included |
| --- | --- |
| **Windows** | Movable, resizable, collapsible windows; close controls; scrolling; left, right, top, bottom, and fill docking slots |
| **Inputs** | Buttons, animated checkboxes, radio buttons, integer/float sliders, single-line text, multiline text, password and read-only modes |
| **Navigation** | Mouse interaction plus Tab, Shift+Tab, arrows, Enter, Space, Escape, Home, End, Backspace, and Delete |
| **Selection** | Combo boxes, dropdown lists, tab bars, tree nodes, collapsible sections, popup windows, and modal dialogs |
| **Data** | Sortable tables, selectable rows, progress bars, fixed-capacity streaming series, and frame events |
| **Charts** | Line, bar, area, scatter, pie, candlestick, histogram, and multi-series real-time plots |
| **Rich content** | Markdown headings, lists, quotes, rules, code blocks, interactive links, callback-resolved images, and texture images |
| **Visuals** | Rounded geometry, shadows, gradients, GPU framebuffer blur, translucent glass cards, and animated state transitions |
| **Themes** | Dark/light presets, smooth transitions, semantic colour tokens, live Theme Studio, and JSON persistence |
| **Fonts** | Runtime Windows font selection, configurable fallback, proportional advances, UTF-8 decoding, CJK/script ranges, and emoji fallback |
| **Editors** | Draggable node canvas with pins, curved links, clipping, and application-owned serializable positions |
| **Platform** | Per-monitor DPI scaling, framebuffer scaling, UTF-8 clipboard integration, Win32 input, and optional GLFW input |

## Text, fonts, and UTF-8

`InputText` stores UTF-8 and moves or deletes by complete codepoint, so editing
does not split multibyte characters. The Win32 baker uses a three-stage font
chain:

1. the selected primary family;
2. a configurable international fallback;
3. Segoe UI Emoji for supplementary-plane symbols.

```cpp
OG_ImplWin32_SetFont("Segoe UI", 17, FW_NORMAL);
OG_ImplWin32_SetFontFallback("Microsoft YaHei UI");

char title[256] = "OSGui";
og::InputText("Window title", title, sizeof(title));
og::InputTextMultiline("Notes", notes, sizeof(notes), og::Vec2(-1, 100));
```

UTF-8 decoding, clipboard conversion, CJK, common script ranges, and emoji are
implemented. Complex shaping and bidirectional layout are not yet implemented;
applications that require fully shaped Arabic or Indic typography should treat
that as a current limitation.

## Modern visual systems

### GPU glass

Backdrop blur travels through the normal draw-command stream. The included
OpenGL compatibility backend copies the live framebuffer and applies a GLSL
blur/tint shader inside the requested shape. Other backends preserve the
translucent fallback surface when that effect path is unavailable.

```cpp
og::GlassCard(
    "Live framebuffer blur + shader tint",
    og::Vec2(-1, 64),
    7.0f
);
```

### Smooth themes and Theme Studio

```cpp
if (og::Button("Daylight"))
    og::SetTheme(og::Theme_Light, 0.35f);

float panel_alpha = og::Animate(
    "settings-panel",
    settings_open ? 1.0f : 0.0f
);
```

Theme transitions interpolate colour, spacing, rounding, shadows, and motion
timing. `ShowThemeEditor()` exposes the same live tokens used by application
widgets.

<table>
<tr>
<td align="center"><strong>Midnight</strong></td>
<td align="center"><strong>Daylight</strong></td>
</tr>
<tr>
<td width="50%"><img src="docs/demo.png" alt="OSGui dark theme"></td>
<td width="50%"><img src="docs/demo-light.png" alt="OSGui light theme"></td>
</tr>
</table>

## Advanced data visualization

Charts accept ordinary arrays or a fixed-capacity `StreamingSeries`. Multiple
series share one scale, while dense line data is reduced toward the visible
pixel width before geometry is emitted.

```cpp
static og::StreamingSeries frame_time(2048);
frame_time.Push(delta_ms);

if (og::BeginChart("Frame time", og::Vec2(-1, 190))) {
    og::ChartArea("CPU", cpu_values, cpu_count);
    og::ChartScatter("samples", points, point_count);
    og::ChartLine("live", frame_time, og::GetColorU32(og::Col_Success));
    og::EndChart();
}
```

Financial or range visualizations can use `ChartCandlesticks`, while
proportional summaries can use `ChartPie`.

## Sortable tables

```cpp
if (og::BeginTable("processes", 3,
        og::TableFlags_Borders | og::TableFlags_RowBg | og::TableFlags_Sortable)) {
    og::TableHeader("Process");
    og::TableHeader("CPU");
    og::TableHeader("State");

    og::TableSelectable("Renderer");
    og::TableSelectable("12.4%");
    og::TableSelectable("Running", true);

    if (const og::TableSortSpec* sort = og::TableGetSortSpec()) {
        if (sort->dirty)
            SortRows(sort->column, sort->direction);
    }
    og::EndTable();
}
```

## Interactive Markdown and images

```cpp
og::SetMarkdownLinkCallback(OpenURL);
og::SetMarkdownImageResolver(ResolveTexture);

og::Markdown(
    "## Build status\n"
    "- Core tests passing\n"
    "[Open documentation](https://example.com/docs)\n"
    "![Preview](texture://preview)"
);
```

Links emit `Event_LinkActivated` and invoke the optional callback. Image URLs
are resolved by the host application into a backend texture ID and size, which
keeps file, network, and GPU resource policy outside the core.

## Node editor

Node positions remain in application state instead of a hidden document tree.

```cpp
static og::Vec2 source_pos(24, 38);
static og::Vec2 filter_pos(240, 126);

if (og::BeginNodeEditor("pipeline", og::Vec2(-1, 320))) {
    og::NodePin samples, stream;

    if (og::BeginNode(1, "Telemetry", &source_pos)) {
        samples = og::NodeOutput("Samples");
        og::EndNode();
    }
    if (og::BeginNode(2, "Smoothing", &filter_pos)) {
        stream = og::NodeInput("Stream");
        og::EndNode();
    }

    og::NodeLink(samples, stream);
    og::EndNodeEditor();
}
```

## Layout, docking, and persistence

Sequential layout remains the default. Grids provide aligned dashboards, and
docking slots provide deterministic tool layouts without a heavyweight docking
tree.

```cpp
og::SetNextWindowDock(og::Dock_Right);
og::Begin("Inspector");

if (og::BeginGrid("properties", 2, 14.0f)) {
    DrawGeneralSettings();
    og::NextGridColumn();
    DrawAdvancedSettings();
    og::EndGrid();
}

og::End();

og::SaveStateJSON("workspace.json");
og::LoadStateJSON("workspace.json");
```

The JSON state includes window positions, sizes, collapse state, docking slots,
UI scale, theme metrics, and the complete semantic colour array.

## Backend matrix

| Component | Status | Notes |
| --- | --- | --- |
| Portable core | Ready | C++11; no OS or graphics headers |
| Win32 platform | Ready | Mouse, keyboard, Unicode input, clipboard, per-monitor DPI, font fallback |
| GLFW platform | Ready, optional | Windows/Linux input, clipboard, framebuffer scale, host-provided font builder |
| OpenGL compatibility | Ready | Zero-loader Windows demo plus GLSL framebuffer blur |
| OpenGL 3 | Ready | Shader, VAO, dynamic VBO/EBO; accepts GLFW, SDL, WGL, or GLX proc loader |
| DirectX 11 | Ready | Dynamic GPU buffers, shaders, font texture, scissor clipping, texture registry |
| Vulkan | Planned | The draw-data contract is already renderer-neutral |

### Modern OpenGL 3

```cpp
OG_ImplOpenGL3_Init(
    reinterpret_cast<OG_GLGetProcAddress>(glfwGetProcAddress)
);

OG_ImplOpenGL3_NewFrame();
// Build UI and call og::Render()
OG_ImplOpenGL3_RenderDrawData(og::GetDrawData());
```

### DirectX 11

```cpp
OG_ImplDX11_Init(device, immediate_context);

OG_ImplDX11_NewFrame();
// Build UI and call og::Render()
OG_ImplDX11_RenderDrawData(og::GetDrawData());
```

Custom DX11 images are registered with `OG_ImplDX11_RegisterTexture` and then
passed to `og::Image` or returned by the Markdown image resolver.

### GLFW on Linux

Enable `OSGUI_BUILD_GLFW_BACKEND`, provide a GLFW window, and supply a font
builder appropriate for your application (for example FreeType or your engine's
existing atlas builder).

```sh
cmake -S . -B build \
  -DOSGUI_BUILD_DEMO=OFF \
  -DOSGUI_BUILD_DX11=OFF \
  -DOSGUI_BUILD_GLFW_BACKEND=ON
cmake --build build --parallel
```

## Examples

The reusable examples in `examples/osgui_examples.cpp` cover common real
applications:

- settings panel with text, combos, toggles, and save notifications;
- launcher with release channels, validation, progress, and glass status;
- sortable file browser with selectable rows and filtering;
- live performance monitor with streaming and candlestick charts.

Call any example after `og::NewFrame()`:

```cpp
og::ShowSettingsExample(&show_settings);
og::ShowLauncherExample(&show_launcher);
og::ShowFileBrowserExample(&show_files);
og::ShowPerformanceMonitorExample(&show_performance);
```

## Architecture

```text
Application / game loop
        |
        +-- platform backend ------ mouse, keys, UTF-8, clipboard, DPI
        |
        v
Immediate widget API
        |
        +-- sequential + grid + docking layout
        +-- focus navigation + frame event queue
        +-- animation + theme transition cache
        +-- charts + Markdown + nodes + tables
        |
        v
Renderer-neutral DrawData
        |
        +-- vertices + indices + clip rectangles
        +-- texture IDs + shader-effect commands
        |
        +-- OpenGL compatibility / OpenGL 3 / DirectX 11
```

Only small, ID-keyed state survives between frames: animation values, text
cursors, navigation focus, popup state, window layout, theme transitions, and
streaming buffers. There is no retained widget hierarchy.

See [docs/architecture.md](docs/architecture.md) for the frame pipeline and
backend contract.

## Repository structure

```text
osgui/
├── osgui.h / osgui.cpp              portable core and public API
├── osgui_impl_win32.*               Windows platform, DPI, clipboard, fonts
├── osgui_impl_glfw.*                optional cross-platform platform backend
├── osgui_impl_opengl2.*             compatibility renderer + GPU blur
├── osgui_impl_opengl3.*             modern shader/VBO renderer
├── osgui_impl_dx11.*                DirectX 11 renderer
├── osgui_demo.cpp                   Studio Dashboard
├── osgui_theme_editor.cpp           live design-token editor
├── examples/                        settings, launcher, files, performance
├── tests/                            headless core tests
├── .github/workflows/build.yml      Windows and Linux CI
├── docs/                             screenshots and architecture notes
├── CMakeLists.txt                   reusable targets and options
└── LICENSE                          MIT License
```

## CMake targets

| Target | Purpose |
| --- | --- |
| `osgui` | Portable core |
| `osgui_examples` | Reusable example windows |
| `osgui_opengl3` | Modern OpenGL renderer |
| `osgui_win32_opengl2` | Win32 plus compatibility OpenGL demo backends |
| `osgui_dx11` | DirectX 11 renderer |
| `osgui_glfw` | Optional GLFW platform backend |
| `osgui_tests` | Headless automated tests |

Useful options:

```text
OSGUI_BUILD_DEMO=ON
OSGUI_BUILD_TESTS=ON
OSGUI_BUILD_EXAMPLES=ON
OSGUI_BUILD_DX11=ON
OSGUI_BUILD_GLFW_BACKEND=OFF
```

## Testing

The core test builds a headless font atlas, generates a representative UI,
checks draw output, edits a multibyte UTF-8 value, verifies codepoint-safe
Backspace, and round-trips JSON state.

```sh
cmake -S . -B build -DOSGUI_BUILD_DEMO=OFF
cmake --build build --config Release
ctest --test-dir build -C Release --output-on-failure
```

GitHub Actions runs the portable core, examples, OpenGL 3 renderer, and tests on
Ubuntu, plus the full Win32, OpenGL, and DirectX 11 build on Windows.

## Project status

OSGui is an early-stage but working library. The current Windows demo, portable
core, OpenGL 3 renderer, DirectX 11 renderer, examples, and test executable all
compile cleanly with MSVC warning level 4. The public API may still evolve while
the project works toward stronger text shaping, drag-and-drop docking trees,
multi-pass blur for every renderer, and a Vulkan backend.

## License

OSGui is available under the [MIT License](LICENSE). Commercial use,
modification, distribution, and private use are permitted with the copyright
and license notice preserved.

<div align="center">

Built for native tools that should feel like products—not debug panels.

</div>
