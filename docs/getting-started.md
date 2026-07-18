# Getting started with OSGui v2

OSGui v2 is an alpha release of an immediate-mode C++ UI library for native tools. It is usable for experimentation and internal tooling, but its API and serialized state may still change before a stable v2 release. It is not yet a drop-in replacement for Dear ImGui; see [Current limits](#current-limits) before choosing it for a project.

## Requirements

- CMake 3.20 or newer
- A C++11 compiler
- A graphics and platform integration supported by your application, or a custom backend

The core library is backend-neutral. The repository also contains Win32/OpenGL 2, Win32/DirectX 11, GLFW, and OpenGL 3 integration code. Backend coverage is not uniform; the [README backend matrix](../README.md#backend-capabilities) records the current differences.

## Build the repository

Clone the public repository and configure a build directory:

```sh
git clone https://github.com/qtWillyG/osGUI.git
cd osGUI
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug -DBUILD_TESTING=ON -DOSGUI_BUILD_TESTS=ON
cmake --build build --config Debug
ctest --test-dir build -C Debug --output-on-failure
```

On a multi-configuration generator such as Visual Studio, `--config Debug` selects the configuration at build and test time. On a single-configuration generator, `CMAKE_BUILD_TYPE` selects it during configuration.

To build the repository examples as well:

```sh
cmake -S . -B build -DOSGUI_BUILD_EXAMPLES=ON
cmake --build build --config Release
```

Only enable the example and backend targets needed by your platform and toolchain. Consult `cmake -LAH -N build` after configuration for the options exposed by the current checkout.

## Consume an installed package

Install OSGui to a prefix:

```sh
cmake --install build --config Release --prefix ./install
```

Then request the core package from your own CMake project:

```cmake
find_package(OSGui 2.0 CONFIG REQUIRED COMPONENTS Core)

add_executable(my_tool main.cpp)
target_link_libraries(my_tool PRIVATE OSGui::Core)
```

Backend components are optional. Installed configurations can expose `OpenGL3`, `Win32OpenGL2`, `DX11`, and `GLFW` components when those targets were built and installed:

```cmake
find_package(OSGui 2.0 CONFIG REQUIRED COMPONENTS Core OpenGL3 GLFW)
target_link_libraries(my_tool PRIVATE OSGui::Core OSGui::OpenGL3 OSGui::GLFW)
```

Keep the requested components aligned with the backends you actually build. `OSGui::Core` alone is sufficient for a custom platform and renderer integration.

## Minimal application lifecycle

Create a context, verify the compile-time and runtime versions, initialize the selected backends, and make the context current before the first frame:

```cpp
#include <osgui.h>

og::Context* context = og::CreateContext();
if (!OG_CHECKVERSION()) {
    og::DestroyContext(context);
    return false;
}
og::SetCurrentContext(context);

// Initialize the platform backend and renderer backend here.
```

`CreateContext()` makes the new context current. Calling `SetCurrentContext()` explicitly is still useful when an application owns more than one context or hands UI work between modules.

A frame has four stages:

```cpp
// 1. Let the platform and renderer backends prepare their frame data.
// PlatformBackend_NewFrame();
// RendererBackend_NewFrame();

// 2. Start the OSGui frame.
og::NewFrame();

// 3. Declare the interface.
if (og::Begin("Inspector")) {
    og::Text("OSGui v2 alpha");

    static float gain = 0.5f;
    og::SliderFloat("Gain", &gain, 0.0f, 1.0f);
}
og::End();

// Optional during development: report unbalanced stacks or invalid state.
og::ValidateState();

// 4. Finalize and submit the draw data.
og::Render();
og::DrawData* draw_data = og::GetDrawData();
// RendererBackend_RenderDrawData(draw_data);
```

The exact backend entry-point names depend on the selected `osgui_impl_*.h` integration. Follow its header and example rather than copying names from another backend.

At shutdown, release renderer and platform resources before destroying the context:

```cpp
// RendererBackend_Shutdown();
// PlatformBackend_Shutdown();
og::DestroyContext(context);
```

Do not destroy textures, font data, or graphics devices while a renderer backend can still reference them.

## Feeding input from a custom platform backend

OSGui v2 records input events through `IO` helper methods. A backend should translate native events into this named-key and pointer API instead of writing OSGui's state arrays directly:

```cpp
og::IO& io = og::GetIO();

io.AddMousePosEvent(mouse_x, mouse_y);
io.AddMouseButtonEvent(0, left_button_down);
io.AddMouseWheelEvent(wheel_x, wheel_y);
io.AddKeyEvent(og::Key_Enter, enter_down);
io.AddInputCharacter(static_cast<unsigned int>('A'));
io.AddFocusEvent(window_has_focus);
```

Set the backend name and only the `BackendFlags` that the integration actually supports. Focus-loss handling matters: report it so OSGui can avoid leaving keys or pointer buttons stuck after the native window loses focus.

Applications using the Win32 backend must forward relevant window messages to its handler. Applications using the GLFW platform backend must also arrange a compatible font-atlas upload through their renderer integration; the platform layer does not invent renderer resources.

See [Architecture: input contract](architecture.md#input-contract) for the event lifecycle and ownership rules.

## Stable identity and scoped state

Visible labels are not always unique. Use ID scopes around repeated controls so the same label produces distinct persistent state:

```cpp
for (int row = 0; row < row_count; ++row) {
    og::PushID(row);
    og::Checkbox("Enabled", &rows[row].enabled);
    og::PopID();
}
```

Scoped style and disabled state use paired push/pop calls:

```cpp
og::PushStyleColor(og::Col_Button, OG_COL32(51, 115, 204, 255));
og::PushItemWidth(180.0f);

og::SliderFloat("Exposure", &exposure, -4.0f, 4.0f);

og::PopItemWidth();
og::PopStyleColor();
```

Every `Begin`/`End`, `BeginChild`/`EndChild`, and push/pop pair must balance in the same frame. `ValidateState()` and the built-in Metrics Studio are designed to expose mistakes while developing an integration.

## Large lists and child regions

Use a child region to create an independently scrollable area, and `ListClipper` to submit only rows that can be visible:

```cpp
og::BeginChild("event_log", og::Vec2(0.0f, 260.0f), og::ChildFlags_Borders);

og::ListClipper clipper;
clipper.Begin(event_count);
while (clipper.Step()) {
    for (int i = clipper.display_start; i < clipper.display_end; ++i) {
        og::Text("Event %d", i);
    }
}
clipper.End();

og::EndChild();
```

The clipper reduces item submission work; it does not virtualize your application data. Keep item heights predictable for the best result.

## State persistence

OSGui can save and load v2 JSON state through files or memory. This is intended for OSGui-owned UI state, not as a general application data format.

```cpp
og::SaveStateJSON("ui-state.json");

// Later, with a compatible current context:
if (!og::LoadStateJSON("ui-state.json")) {
    const char* error = og::GetLastError();
    // Log or display error.
}
```

Treat state files as versioned input. The file loader enforces a 16 MiB limit, checks the supported schema range, clamps accepted numeric fields, and reports failures through `GetLastError()`. Re-save successfully imported older state to move it to the v2 schema; do not assume future alpha builds will preserve every serialized field.

## Draw-data snapshots

`GetDrawData()` exposes data owned by the current context. It is normally consumed immediately after `Render()`. If another subsystem needs an owned copy - for capture, deferred inspection, or controlled handoff - use `DrawDataSnapshot` rather than retaining raw pointers into live frame storage.

Snapshots solve draw-data lifetime, not graphics-resource lifetime. The application still owns every texture represented by a `TextureID`, and the receiving renderer must understand callbacks, vertex offsets, clip rectangles, and effect fallbacks.

## Themes, contrast, and motion

The built-in themes are Dark, Light, and High Contrast. Reduced-motion mode suppresses optional motion so an application can respect an accessibility preference. These features improve baseline usability, but they do not replace application-level accessibility testing, semantic navigation, or platform assistive-technology integration.

## Troubleshooting

### Version check fails

Headers and the linked OSGui library come from different builds. Remove stale artifacts, rebuild, and verify that the include path and linked package resolve to the same installation.

### An API reports that no context is current

Create a context before calling frame or widget APIs, and call
`SetCurrentContext()` when switching between contexts owned by the current
thread. Keep each context on its creating thread for its entire lifetime,
including destruction; selecting a context is not a thread-migration mechanism.

### Text is blank or incorrect

Verify that the font atlas was built, uploaded by the renderer backend, and assigned a live texture for the duration of rendering. The Win32 GDI atlas path is deliberately bounded and does not provide full Unicode shaping, bidirectional layout, or exhaustive CJK coverage.

### Keyboard or mouse input remains stuck

Forward focus events and both press and release transitions. For Win32, make sure the native message handler receives the messages documented by the backend. For GLFW, ensure callbacks are installed or chained as expected by the integration.

### A blur or tint effect is missing

Effects are capability-dependent. Backends must provide a visible, documented fallback when an effect is unavailable. OpenGL 2 blur uses an expensive framebuffer-copy path, while OpenGL 3 and DirectX 11 currently fall back to tint for unsupported effects.

### UI state looks corrupted after an early return

Audit every `Begin`/`End` and push/pop pair, then call `ValidateState()` and inspect Metrics Studio. Early-return paths are a common source of stack imbalance in immediate-mode code.

## Current limits

The v2 alpha does not yet provide:

- IME composition, grapheme-cluster-aware editing, bidirectional layout, and
  full native desktop text-control semantics;
- a complete docking system;
- multi-viewport platform windows;
- a Vulkan renderer;
- feature parity or performance parity with Dear ImGui across all workloads.

Start with the [architecture guide](architecture.md) when writing a backend, and read the [v2 migration guide](migration-v2.md) before porting a 0.x integration.
