# osGUI

A tiny **immediate-mode GUI** library written in C++, structured exactly like
[Dear ImGui](https://github.com/ocornut/imgui): a portable core that emits draw
lists of vertices/indices, a **platform backend** that feeds input, and a
**renderer backend** that draws the geometry.

![demo](docs/demo.png)

This is **not** Dear ImGui — it's a small homage that follows the same
architecture and visual style (the "Dark" theme colors, the famous
`(0.45, 0.55, 0.60)` clear color, ProggyClean-ish monospace text).

## File layout (mirrors Dear ImGui)

| osGUI                      | Dear ImGui equivalent                       |
|----------------------------|---------------------------------------------|
| `osgui.h` / `osgui.cpp`    | `imgui.h` / `imgui.cpp` (core)              |
| `osgui_demo.cpp`           | `imgui_demo.cpp` (the demo window)          |
| `osgui_impl_win32.cpp`     | `imgui_impl_win32.cpp` (platform backend)   |
| `osgui_impl_opengl2.cpp`   | `imgui_impl_opengl2.cpp` (renderer backend) |
| `main.cpp`                 | `examples/example_win32_opengl3/main.cpp`   |

## Build (Windows / MSVC)

Everything links against libraries that ship with Windows
(`user32`, `gdi32`, `opengl32`) — **no GLFW / GLEW / GLAD / external downloads.**

```bat
build.bat
```

or manually from a *Developer Command Prompt*:

```bat
cl /EHsc /O2 /MD main.cpp osgui.cpp osgui_demo.cpp ^
   osgui_impl_win32.cpp osgui_impl_opengl2.cpp ^
   /Fe:osgui_demo.exe ^
   /link user32.lib gdi32.lib opengl32.lib /SUBSYSTEM:WINDOWS
```

### MinGW / g++

```sh
g++ main.cpp osgui.cpp osgui_demo.cpp osgui_impl_win32.cpp osgui_impl_opengl2.cpp \
    -o osgui_demo.exe -lopengl32 -lgdi32 -luser32 -mwindows
```

## Usage (the immediate-mode loop)

```cpp
og::CreateContext();
OG_ImplWin32_Init(hwnd);
OG_ImplOpenGL2_Init();

while (running) {
    OG_ImplOpenGL2_NewFrame();
    OG_ImplWin32_NewFrame();
    og::NewFrame();

    og::Begin("Hello, world!");
    if (og::Button("Click me")) counter++;
    og::SameLine();
    og::Text("counter = %d", counter);
    og::SliderFloat("float", &f, 0.0f, 1.0f);
    og::End();

    og::Render();
    glClear(GL_COLOR_BUFFER_BIT);
    OG_ImplOpenGL2_RenderDrawData(og::GetDrawData());
    SwapBuffers(hdc);
}
```

## What's implemented

- **Core**: context, draw list (vertex/index buffers + clip-rect command list),
  font atlas, ID stack with `##` label hashing, hover/active interaction model.
- **Windows**: draggable title bar, collapse arrow, close button, resize grip,
  mouse-wheel scrolling + scrollbar, focus / z-ordering, `SetNextWindowPos/Size`.
- **Widgets**: `Text` / `TextDisabled` / `TextColored` / `BulletText`, `Button`,
  `SmallButton`, `Checkbox`, `RadioButton`, `SliderFloat`, `SliderInt`,
  `CollapsingHeader`, `TreeNode`/`TreePop`, `Separator`, `Spacing`,
  `Indent`/`Unindent`, `SameLine`, `ProgressBar`, `PlotLines`, `PlotHistogram`.
- **Platform backend (Win32)**: mouse/wheel/keyboard/char input, timing,
  display size, GDI font-atlas baking.
- **Renderer backend (OpenGL 1.x)**: ortho projection, per-command scissor,
  textured triangle draw via client arrays.

## Scope / honesty

Dear ImGui is ~60k lines with 15+ backends (GLFW, SDL2/3, DX9–12, Vulkan, Metal,
WebGPU, OSX, Android…). This is a focused subset that demonstrates the **same
architecture** with **one** platform backend and **one** renderer backend — the
canonical Win32 + OpenGL pair.

Adding another backend follows the exact same interface:

- **A new renderer** (e.g. `osgui_impl_dx11.cpp`) only needs to implement
  `CreateFontsTexture()` (upload `og::GetFontAtlas().pixels`) and
  `RenderDrawData()` (walk `og::DrawData` -> per `DrawCmd`: set scissor from
  `clip_rect`, bind `tex_id`, draw `elem_count` indices from `idx_offset`).
- **A new platform** (e.g. `osgui_impl_sdl2.cpp`) only needs to fill `og::IO`
  (mouse, wheel, keys, chars, `display_size`, `delta_time`) each frame.

Not implemented vs. real ImGui: docking/viewports, tables, full text editing
(`InputText` caret/selection), combo/menu popups, drag-drop, multi-viewport,
TrueType atlas baking via stb_truetype (osGUI bakes via GDI instead).
