# Migrating to OSGui v2 alpha

This guide covers the source and integration changes from the earlier OSGui API to `2.0.0-alpha.1`. The alpha is intentionally not source-, binary-, or serialized-state compatible with every 0.x build. Migrate one boundary at a time: context lifecycle, input, renderer data, UI code, then persistence and packaging.

## Migration checklist

1. Rebuild OSGui and all backends from the same checkout.
2. Add the runtime version check after creating the context.
3. Replace integer texture assumptions with `og::TextureID`.
4. Update custom renderers for vertex offsets, callbacks, logical clip rectangles, and effect fallbacks.
5. Feed queued input events and named keys from the platform backend.
6. Balance every new scope and give repeated items stable IDs.
7. Re-save accepted UI state in schema version 2.
8. Switch CMake consumers to the exported `OSGui::` targets.
9. Run `ValidateState()`, backend examples, and the test suite before shipping the migrated build.

## Version and context lifecycle

V2 exposes explicit context management and a compile-time/runtime compatibility check:

```cpp
og::Context* context = og::CreateContext();
OG_CHECKVERSION();
```

`CreateContext()` makes the returned context current. If an application owns more than one context, set the intended one before accessing context-bound state:

```cpp
og::SetCurrentContext(context);
og::IO& io = og::GetIO();
```

Destroy the matching context after its backends have shut down:

```cpp
og::DestroyContext(context);
```

The current-context slot is thread-local, but the context itself is not designed for simultaneous mutation from multiple threads. Coordinate ownership instead of using thread-local selection as a synchronization mechanism.

## Texture identifiers are pointer-sized

`og::TextureID` is now based on `uintptr_t`. Code that stored every texture in an `unsigned int` can truncate native pointers or 64-bit handles.

Before:

```cpp
unsigned int texture = CreateTexture();
```

V2:

```cpp
og::TextureID texture = static_cast<og::TextureID>(native_integer_handle);
```

For a pointer-backed renderer, convert through `uintptr_t` explicitly at the backend boundary:

```cpp
og::TextureID id = reinterpret_cast<uintptr_t>(native_texture_pointer);
NativeTexture* texture = reinterpret_cast<NativeTexture*>(id);
```

Update application image records, renderer lookup tables, font-atlas handles, Markdown image resolvers, and serialization code together. A `TextureID` is an opaque application/backend contract; OSGui does not own or destroy the resource it names.

## The draw contract is richer

V2 draw commands can carry:

- an index offset and a vertex offset;
- a logical-coordinate clip rectangle;
- a texture ID and effect descriptor;
- a user callback and callback data.

A custom renderer must not assume every command is a simple indexed draw from vertex zero. For every `DrawCmd`:

1. dispatch its callback when present instead of issuing an ordinary draw;
2. transform and clamp its logical clip rectangle for framebuffer scale and renderer origin;
3. bind the referenced texture without narrowing `TextureID`;
4. honor both index and vertex offsets;
5. implement the requested effect or an explicit visible fallback;
6. restore graphics state that the backend changes before returning to the host application.

Nested clip rectangles now intersect by default. If a custom draw-list extension relied on replacing the parent clip, review every `PushClipRect` call and opt out only where that behavior is intentional.

`GetDrawData()` remains live context-owned frame data. Use `DrawDataSnapshot` when data must outlive the frame or be handed to a deferred consumer. A snapshot owns copied draw data, not the referenced GPU textures.

## Input is queued and keys are named

Platform backends should submit transitions through `og::IO` helpers:

```cpp
og::IO& io = og::GetIO();
io.AddMousePosEvent(x, y);
io.AddMouseButtonEvent(0, is_down);
io.AddMouseWheelEvent(wheel_x, wheel_y);
io.AddKeyEvent(og::Key_Escape, escape_down);
io.AddInputCharacter(codepoint);
io.AddFocusEvent(has_focus);
```

The helpers update the current `IO` state and retain an event record for the frame's diagnostics. `NewFrame()` computes presses and releases from that accumulated state, and `Render()` clears the frame event record. Use the `og::Key` enumeration for portable application shortcuts. The integer-key query overload remains for compatibility, but new code should use named keys.

The old fixed/count-style character input assumptions no longer apply: character input is stored as a vector, and `input_char_count` has been removed. Backend code should call `AddInputCharacter()` rather than writing character buffers directly.

Key-state storage is sized by `Key_COUNT`. Code that copied a hard-coded number of key entries must use the declared key count instead. Set backend names and `BackendFlags` accurately so applications can reason about available integration features.

## Windows, child regions, and conditions

`Begin()` accepts window flags, and next-window setters accept conditions so callers can distinguish first-use defaults from unconditional changes. Review wrappers that forwarded the old argument list.

V2 also adds size constraints, window queries, scrolling helpers, child regions, and `ListClipper`. Prefer `BeginChild()` for a bounded scrollable panel and the clipper for large uniform lists. Keep all begin/end pairs balanced, even when `Begin()` or `BeginChild()` reports a collapsed or clipped region.

## IDs and state scopes

V2 exposes `GetID()`, `PushID()` overloads, and `PopID()`. Use them wherever labels repeat:

```cpp
for (int i = 0; i < count; ++i) {
    og::PushID(i);
    og::Checkbox("Visible", &rows[i].visible);
    og::PopID();
}
```

Style colors, style variables, item widths, disabled state, textures, effects, and clip rectangles use stacks. Porting code should treat every push as a scoped obligation and add validation around early-return paths.

## Item queries, drag and drop, and widgets

V2 tracks the most recently submitted item for hover, active, focus, and rectangle queries. Drag-and-drop sources and targets also derive identity and state from the item pipeline. Submit or query them immediately around the intended item; unrelated widgets replace last-item state.

The widget set now includes additional controls and plotting/editor modules, but their presence does not imply full Dear ImGui behavioral parity. Preserve application-side validation and test keyboard, clipping, focus, and disabled-state behavior for each newly adopted control.

## Themes and motion

Dark, Light, and High Contrast themes are available, along with reduced-motion behavior. If the application previously overwrote the complete style every frame, migrate it to a base theme plus deliberate overrides so new style fields keep sensible defaults.

High Contrast is a visual theme, not a complete accessibility layer. Reduced motion affects optional animation; it does not provide platform semantics, screen-reader integration, or editable-text IME support.

## Persistence moves to schema version 2

V2 can load and save JSON state through files and memory. The serializer writes schema version 2; the loader checks the supported schema range, clamps accepted numeric fields, limits file reads to 16 MiB, and exposes an error string with `GetLastError()`.

Treat older files as import data:

1. keep a backup while migrating;
2. attempt the load and surface `GetLastError()` on failure;
3. verify window and application-visible state;
4. save again to produce canonical v2 state.

Do not depend on undocumented JSON field order or fields from an alpha build. Do not use OSGui's state file as the sole store for domain data that your application cannot reconstruct.

## CMake package migration

V2 publishes versioned package metadata and namespaced targets. Replace ad hoc include/library paths with an installed package where practical:

```cmake
find_package(OSGui 2.0 CONFIG REQUIRED COMPONENTS Core)
target_link_libraries(my_tool PRIVATE OSGui::Core)
```

Request optional components only when the installed build contains them:

```cmake
find_package(OSGui 2.0 CONFIG REQUIRED COMPONENTS Core OpenGL3 GLFW)
target_link_libraries(my_tool PRIVATE OSGui::Core OSGui::OpenGL3 OSGui::GLFW)
```

The exported component names are `Core`, `Examples`, `OpenGL3`, `Win32OpenGL2`, `DX11`, and `GLFW`. Re-run configuration after changing component sets; do not mix a v2 core with backend object files from an older build.

## Diagnostics to add during migration

Register a debug callback if the host application has a logging system, inspect `FrameMetrics`, and call `ValidateState()` at a stable point near the end of development frames. Metrics Studio provides an in-UI view of context and frame behavior.

These diagnostics can find invalid stacks and integration mistakes, but the current automated tests do not yet cover every widget and backend. Exercise the exact platform, DPI scale, renderer state, input path, and persistence files used by the application.

## Features still outside the v2 alpha

The current editor supports mouse and keyboard range selection, callback-backed
cut/copy/paste, per-widget undo/redo, and multiline input. It does not provide
IME composition, grapheme-cluster-aware movement, bidirectional layout, or full
native desktop text-control behavior. Complete docking, multi-viewport platform
windows, and a Vulkan renderer also remain outside this alpha. Plan those as
application constraints rather than assuming Dear ImGui parity. The
[architecture guide](architecture.md#known-limitations) records the current
boundaries in more detail.
