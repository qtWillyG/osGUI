<div align="center">

# OSGui

### A modern immediate-mode interface foundation for native C++ tools

Animated controls, fluid themes, flexible grids, native charts, and Markdown -
all without making your application own a retained widget tree.

[![C++](https://img.shields.io/badge/C%2B%2B-11%2B-6C5CE7?style=for-the-badge&logo=cplusplus&logoColor=white)](#requirements)
[![Platform](https://img.shields.io/badge/platform-Windows-00A4EF?style=for-the-badge&logo=windows&logoColor=white)](#requirements)
[![Build](https://img.shields.io/badge/build-passing-22C55E?style=for-the-badge)](#build-from-source)
[![License](https://img.shields.io/badge/license-MIT-8B5CF6?style=for-the-badge)](LICENSE)

[Overview](#why-osgui) · [Quick start](#quick-start) · [Features](#feature-tour) · [Architecture](#architecture) · [Roadmap](#roadmap)

</div>

![OSGui Studio Dashboard in dark mode](docs/demo.png)

## Why OSGui?

OSGui brings the simplicity of immediate-mode UI to interfaces that should
feel like modern applications instead of debug panels. The application still
describes its UI every frame, while small internal caches preserve only the
state needed for animation, interaction, layout, themes, and streaming data.

<table>
<tr>
<td width="33%" valign="top">

### Modern by default

Rounded surfaces, subtle shadows, gradient accents, animated switches, and
semantic design tokens are part of the core visual language.

</td>
<td width="33%" valign="top">

### Advanced but lightweight

Grid layout, real-time charts, Markdown, events, and motion sit behind a small
immediate API with no retained widget objects.

</td>
<td width="33%" valign="top">

### Backend focused

The core produces renderer-neutral draw lists. The included Windows demo needs
no downloaded windowing, OpenGL loader, or GUI dependencies.

</td>
</tr>
</table>

## Quick start

### 1. Build the demo

```bat
cd path\to\osgui
build.bat
osgui_demo.exe
```

The build script uses an existing Visual Studio developer environment or finds
the latest installed C++ toolchain with `vswhere`.

### 2. Initialize OSGui

```cpp
og::CreateContext();
OG_ImplWin32_Init(hwnd);
OG_ImplOpenGL2_Init();
```

### 3. Describe the interface each frame

```cpp
OG_ImplOpenGL2_NewFrame();
OG_ImplWin32_NewFrame();
og::NewFrame();

og::Begin("Project settings");

static bool preview = true;
static float opacity = 0.75f;

og::Checkbox("Live preview", &preview);
og::SliderFloat("Opacity", &opacity, 0.0f, 1.0f);

if (og::Button("Apply changes"))
    ApplySettings();

og::End();

og::Render();
OG_ImplOpenGL2_RenderDrawData(og::GetDrawData());
```

## Feature tour

| System | What is available now |
| --- | --- |
| **Motion** | ID-keyed animation state, hover fades, animated switches, radio transitions, and application-defined values |
| **Themes** | Built-in dark and light presets with interpolated colors, spacing, radii, shadows, and timing |
| **Layout** | Sequential layout, same-line placement, indentation, scrolling windows, and multi-row grids |
| **Charts** | Line and bar series, shared scaling, legends, fills, grid lines, and pixel-aware downsampling |
| **Streaming** | Fixed-capacity `StreamingSeries` ring buffers designed for real-time telemetry |
| **Rich content** | Headings, lists, quotes, rules, fenced code, link labels, and inline Markdown markers |
| **Events** | Click, value-change, and window-close events alongside normal widget return values |
| **Rendering** | Backend-neutral vertices, indices, texture IDs, draw commands, and clip rectangles |

### Smooth dark and light themes

Themes transition in place instead of flashing from one palette to another.

```cpp
if (og::Button("Light theme"))
    og::SetTheme(og::Theme_Light, 0.35f);

float visibility = og::Animate(
    "settings-panel",
    settings_open ? 1.0f : 0.0f
);
```

<table>
<tr>
<td align="center"><strong>Dark</strong></td>
<td align="center"><strong>Light</strong></td>
</tr>
<tr>
<td width="50%"><img src="docs/demo.png" alt="OSGui dark theme"></td>
<td width="50%"><img src="docs/demo-light.png" alt="OSGui light theme"></td>
</tr>
</table>

The design is driven by semantic roles such as `Col_WindowBg`, `Col_Link`,
`Col_Success`, `Col_Warning`, and `Col_WindowShadow`. Direct customization is
available through `og::GetStyle()`.

### Grid layout

Ordinary immediate-mode widgets can be arranged into responsive rows without
changing how the widgets themselves are written.

```cpp
if (og::BeginGrid("dashboard", 2, 16.0f)) {
    DrawControls();

    og::NextGridColumn();
    DrawInspector();

    og::NextGridColumn();
    DrawTelemetry();

    og::NextGridColumn();
    DrawActivity();

    og::EndGrid();
}
```

### Native streaming charts

```cpp
static og::StreamingSeries frame_times(2048);
frame_times.Push(delta_time_ms);

if (og::BeginChart("Frame time", og::Vec2(-1, 180))) {
    og::ChartLine("CPU", frame_times);
    og::ChartLine(
        "budget",
        budget_values,
        budget_count,
        og::GetColorU32(og::Col_Warning)
    );
    og::EndChart();
}
```

Multiple line and bar series share a scale. Large line series are reduced to
approximately the visible pixel width before geometry is emitted.

### Built-in Markdown

```cpp
og::Markdown(
    "## Build status\n"
    "- **Core:** ready\n"
    "- **Renderer:** running\n"
    "> No external Markdown extension required."
);
```

The parser is deliberately compact. Full Unicode shaping, interactive links,
images, and cached document trees remain part of the richer text roadmap.

## Requirements

| Requirement | Current demo |
| --- | --- |
| Operating system | Windows 10 or Windows 11 |
| Compiler | Visual Studio with **Desktop development with C++** |
| Language level | C++11 or newer |
| Platform backend | Win32 |
| Renderer backend | OpenGL 1.x/2-compatible fixed function |
| External downloads | None |

## Build from source

### Direct MSVC build

```bat
build.bat
```

This produces `osgui_demo.exe` in the repository root.

### CMake build

```bat
build-cmake.bat
```

Or from an initialized Visual Studio developer terminal:

```bat
cmake -S . -B build -G "NMake Makefiles" -DCMAKE_BUILD_TYPE=Release
cmake --build build
```

CMake exports two reusable targets:

- `osgui` - portable immediate-mode core
- `osgui_win32_opengl2` - included Windows platform and renderer backends

## Architecture

```text
Application UI
      |
      v
Immediate widget API
      |
      +---- interaction + frame events
      +---- animation + theme state
      +---- sequential + grid layout
      +---- charts + Markdown
      |
      v
Backend-neutral DrawData
      |
      +---- Win32 platform backend
      +---- OpenGL renderer backend
```

The platform backend supplies input, timing, display size, and font-atlas data.
The renderer consumes vertices, indices, textures, and clip commands without
needing to understand widgets.

Read the full [architecture guide](docs/architecture.md) for the frame
pipeline, subsystem boundaries, caching strategy, and future backend contract.

## Repository structure

```text
osgui/
|-- osgui.h / osgui.cpp              public API and portable core
|-- osgui_impl_win32.*               Windows platform backend
|-- osgui_impl_opengl2.*             compatibility renderer
|-- osgui_demo.cpp                   Studio Dashboard showcase
|-- main.cpp                         native example host
|-- CMakeLists.txt                   reusable build targets
|-- build.bat / build-cmake.bat      one-command Windows builds
`-- docs/
    |-- architecture.md              technical design guide
    |-- demo.png                     dark-mode showcase
    `-- demo-light.png               light-mode showcase
```

## Roadmap

- [x] Modern animated control foundation
- [x] Smooth semantic dark/light themes
- [x] Sequential and grid layout
- [x] Native line and bar charts
- [x] Real-time streaming series
- [x] Lightweight Markdown rendering
- [x] Structured frame events
- [ ] Shader-based OpenGL 3 renderer
- [ ] DirectX 11 renderer
- [ ] Vulkan renderer
- [ ] Full UTF-8 shaping and font fallback
- [ ] Interactive Markdown links and images
- [ ] GPU blur, higher-quality shadows, and path rendering

## Project status

OSGui is an early but working foundation. The demo and public API compile with
MSVC warning level 4, and both direct and CMake builds are exercised locally.
Expect API refinement while the modern renderer and text systems are developed.

## License

OSGui is released under the [MIT License](LICENSE). Use it in personal or
commercial projects, modify it, and redistribute it while preserving the
license notice.

<div align="center">

Built for small native tools, editors, launchers, overlays, and telemetry panels.

</div>
