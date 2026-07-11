# OSGui

<p align="center">
  <strong>A small, modern immediate-mode GUI for native C++ tools.</strong><br>
  Purpose-built controls, a portable draw-list core, and a dependency-free Windows demo.
</p>

<p align="center">
  <img src="docs/demo.png" alt="OSGui modern control center demo" width="820">
</p>

## Why OSGui?

OSGui is for projects that need a compact native interface without bringing in a large UI framework. It keeps the convenient immediate-mode workflow, but uses its own modern visual language: spacious dark surfaces, rounded controls, vivid violet actions, mint status accents, switch-style checkboxes, and crisp Cascadia Mono typography.

The project is intentionally focused. It is not trying to replace a full desktop application framework—it is a practical foundation for debug panels, launchers, utilities, overlays, editors, and game-development tools.

## Highlights

- **Modern by default** — a cohesive dark theme designed specifically for OSGui.
- **Familiar controls** — buttons, switches, radio buttons, sliders, progress bars, trees, plots, and layout helpers.
- **Tiny integration surface** — one core pair plus a platform and renderer backend.
- **No downloaded dependencies** — the Windows demo uses Win32, GDI, and the OpenGL library included with Windows.
- **Backend-friendly core** — widgets emit vertices, indices, textures, and clip commands instead of talking directly to the OS.
- **Immediate-mode workflow** — describe the current interface each frame; OSGui handles interaction state and draw data.

## Quick start

### 1. Build the included Windows demo

Open an **x64 Native Tools Command Prompt for Visual Studio**, change to this folder, then run:

```bat
build.bat
```

The build uses only libraries shipped with Windows. MinGW users can build with:

```sh
g++ main.cpp osgui.cpp osgui_demo.cpp osgui_impl_win32.cpp osgui_impl_opengl2.cpp \
    -o osgui_demo.exe -lopengl32 -lgdi32 -luser32 -mwindows
```

### 2. Add OSGui to your application

Compile these source files with your project:

```text
osgui.cpp
osgui_impl_win32.cpp
osgui_impl_opengl2.cpp
```

Then initialize a context and both backends after creating your Win32/OpenGL window:

```cpp
og::CreateContext();
OG_ImplWin32_Init(hwnd);
OG_ImplOpenGL2_Init();
```

### 3. Build the interface each frame

```cpp
OG_ImplOpenGL2_NewFrame();
OG_ImplWin32_NewFrame();
og::NewFrame();

og::Begin("Project settings");
static bool preview = true;
static float opacity = 0.75f;

og::Checkbox("Live preview", &preview);
og::SliderFloat("Opacity", &opacity, 0.0f, 1.0f);
if (og::Button("Apply changes")) {
    // Handle the action here.
}
og::End();

og::Render();
OG_ImplOpenGL2_RenderDrawData(og::GetDrawData());
```

## Included widgets

| Category | API |
| --- | --- |
| Text | `Text`, `TextDisabled`, `TextColored`, `BulletText` |
| Actions | `Button`, `SmallButton` |
| Selection | `Checkbox`, `RadioButton` |
| Values | `SliderFloat`, `SliderInt`, `ProgressBar` |
| Structure | `CollapsingHeader`, `TreeNode`, `TreePop` |
| Data | `PlotLines`, `PlotHistogram` |
| Layout | `SameLine`, `Separator`, `Spacing`, `Indent`, `Unindent` |

Windows can be moved, collapsed, resized, closed, focused, layered, and scrolled. Stable widget IDs support the familiar `Visible label##unique_id` pattern.

## Architecture

```text
Application
    |
    +-- OSGui core                  layout, state, widgets, draw lists
    |
    +-- Platform backend (Win32)    input, timing, display size, font atlas
    |
    +-- Renderer backend (OpenGL2)  textures, clipping, indexed triangles
```

| File | Purpose |
| --- | --- |
| `osgui.h` / `osgui.cpp` | Public API, state, layout, widgets, and draw-list generation |
| `osgui_impl_win32.*` | Win32 input, timing, and Cascadia Mono font-atlas creation |
| `osgui_impl_opengl2.*` | Fixed-function OpenGL renderer with no loader dependency |
| `osgui_demo.cpp` | Showcase window used in the screenshot |
| `main.cpp` | Minimal native Windows host application |

## Customization

Theme colors and spacing are available through `og::GetStyle()`:

```cpp
og::Style& style = og::GetStyle();
style.window_padding = og::Vec2(20, 18);
style.colors[og::Col_Button] = OG_COL32(255, 110, 140, 255);
style.colors[og::Col_CheckMark] = OG_COL32(110, 235, 200, 255);
```

## Current scope

OSGui currently ships with one platform backend (Win32) and one renderer backend (OpenGL 1.x/2-compatible fixed function). It does not yet include docking, tables, text editing, pop-up menus, drag and drop, or multi-viewport support.

That limited scope is deliberate: the codebase stays approachable, and the draw-data boundary makes additional backends possible without rewriting the widget layer.

## Project status

OSGui is an early, usable foundation. The API may evolve as more controls and backends are added. If you use it in a project, pin the commit you integrated and review changes before upgrading.

## License

Add a `LICENSE` file before redistributing OSGui or accepting outside contributions.
