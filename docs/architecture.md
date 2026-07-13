# OSGui architecture

OSGui exposes an immediate-mode API while retaining only the state that must
survive a frame. Applications remain the source of truth for their interface;
OSGui stores window placement, interaction IDs, animation channels, theme
transitions, chart ring buffers, and layout state.

## Frame pipeline

```text
platform input
     |
     v
  NewFrame() ------ update theme and animation caches
     |
     v
application UI ---- sequential layout or grid measurement
     |               interaction resolution and event emission
     v
  Render() --------- collect backend-neutral draw lists
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
`EndGrid()`. Grid cells can contain ordinary immediate-mode widgets, Markdown,
or charts.

### Animation and events

`Animate()` is available for application-defined motion. Built-in controls use
the same ID-keyed cache for hover, selection, and switch motion. Interactions
also append small records to the frame-local event queue while preserving the
traditional boolean return values.

### Charts

`StreamingSeries` is a fixed-capacity float ring buffer. The chart builder can
combine line and bar series, calculates a shared axis range, and downsamples
line data to roughly the chart's pixel width before emitting geometry.

### Markdown

The first Markdown layer supports headings, emphasis-marker stripping, lists,
quotes, rules, links, and fenced code blocks. It deliberately uses the same
layout and draw-list primitives as every other widget. UTF-8 shaping, cached
document trees, images, and selectable links belong to a later text-engine
milestone.

## Backend contract

The core emits `DrawData`, containing draw lists of vertices, indices, texture
IDs, and clip rectangles. It does not call Win32 or OpenGL directly. The
current platform backend supplies Win32 input and a GDI font atlas; the current
renderer consumes the draw lists through fixed-function OpenGL.

Future OpenGL 3, DirectX 11, and Vulkan renderers can consume the same core
contract. Shader-based backends will enable true blur, better shadows, signed
distance-field text, and richer gradient effects without changing widget APIs.

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
