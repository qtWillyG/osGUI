# OSGui architecture

OSGui exposes an immediate-mode API while retaining only the state that must
survive a frame. Applications remain the source of truth for their interface;
OSGui stores window placement, docking slots, interaction IDs, keyboard focus,
text cursors, popup state, animation channels, theme transitions, chart ring
buffers, and layout state.

## Frame pipeline

```text
platform input
     |
     v
  NewFrame() ------ key transitions, focus order, themes, animations, overlays
     |
     v
application UI ---- sequential, grid, table, tab, and docking layout
     |               interaction resolution and event emission
     v
  Render() --------- sort windows and append the tooltip/toast overlay
     |
     v
renderer backend --- clipping, textures, indexed triangle batches
```

The public API is immediate. A button is declared and evaluated in the same
call. Persistent behavior is attached to its stable 64-bit ID, so an animated
hover does not require a retained `Button` object.

## Core systems

### State and IDs

Labels are hashed together with the current window or container ID. Entries in
the animation cache record the current value, target, and last frame in which
the ID appeared. Stale entries are periodically reclaimed.

### Themes

`SetTheme()` captures the current style and interpolates it toward a built-in
dark or light theme. Colors, spacing, radii, shadows, and motion timing share
the same transition clock. Widgets consume semantic color roles rather than
hard-coded colors.

### Layout

The standard cursor layout remains the fast path. `BeginGrid()` temporarily
partitions the available content width and restores the parent layout in
`EndGrid()`. Tables use the same temporary-layout approach. Docking slots
resolve window geometry against the display size, while JSON state persists
window and theme data without hiding layout ownership.

### Animation and events

`Animate()` is available for application-defined motion. Built-in controls use
the same ID-keyed cache for hover, selection, and switch motion. Interactions
also append small records to the frame-local event queue while preserving the
traditional boolean return values.

### Charts

`StreamingSeries` is a fixed-capacity float ring buffer. The chart builder can
combine line, bar, area, scatter, pie, and candlestick series, calculates a
shared axis range where appropriate, and downsamples dense line data toward
the chart's pixel width before emitting geometry.

### Markdown

Markdown supports headings, emphasis-marker stripping, lists, quotes, rules,
fenced code blocks, interactive links, and callback-resolved texture images.
It deliberately uses the same layout and draw-list primitives as every other
widget. Full complex-script shaping and cached document trees remain future
text-engine work.

## Backend contract

The core emits `DrawData`, containing draw lists of vertices, indices, texture
IDs, and clip rectangles. It does not call Win32 or OpenGL directly. The
Win32 and GLFW platform backends supply input, clipboard, timing, DPI, and
framebuffer scale. Win32 also provides a runtime-rebuildable proportional GDI
atlas with primary, international, and emoji fallback. The compatibility
renderer handles `DrawEffect_BackdropBlur` through a dynamically loaded GLSL
shader.

The OpenGL 3 backend consumes the contract with shaders, a VAO, and streaming
vertex/index buffers. DirectX 11 uses dynamic buffers, an orthographic constant
buffer, a texture registry, scissor state, and compiled HLSL shaders. Vulkan is
the remaining planned renderer. Future backends can add separable multi-pass
blur, offscreen composition, signed-distance-field text, or GPU path effects
without changing widget APIs.

## Near-term module split

As the implementation grows, the public API can remain in `osgui.h` while the
core is split into:

```text
src/osgui_context.cpp
src/osgui_layout.cpp
src/osgui_animation.cpp
src/osgui_style.cpp
src/osgui_text.cpp
src/osgui_markdown.cpp
src/osgui_charts.cpp
src/osgui_draw.cpp
```

The current consolidated `osgui.cpp` keeps the prototype easy to build while
the subsystem boundaries settle.
