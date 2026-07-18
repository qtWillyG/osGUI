# Changelog

Notable changes to OSGui are recorded in this file. The project is currently in alpha; public APIs, backend contracts, and serialized state may change before the stable v2 release.

The format follows [Keep a Changelog](https://keepachangelog.com/en/1.1.0/), and version numbers follow [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## Unreleased

### Added

- Mouse and keyboard range selection, callback-backed cut/copy/paste,
  per-widget undo/redo, multiline editing, and ordered queued text actions.
- Persistence regression coverage for nested member names, legacy schema v1,
  malformed containers, separators, numbers, and string escapes.

### Fixed

- Workspace JSON members are resolved only within their owning object, so
  nested metadata cannot override top-level, theme, or window fields.
- State loading now rejects mismatched containers, malformed recognized
  values, invalid JSON escapes, and malformed window-array entries while
  preserving transactional rollback.
- State serialization always uses the JSON-required decimal point regardless
  of the host's C++ locale.
- Installed-package CI now validates every optional component built on Windows
  and Linux instead of checking only the core target.
- Documentation now matches the implemented text-editing and context
  thread-affinity contracts.

## 2.0.0-alpha.1 - 2026-07-17

### Added

- Explicit context creation, destruction, current-context selection, and runtime/header version checking through `OG_CHECKVERSION()`.
- Queued mouse, wheel, keyboard, character, and focus events; named keys; backend names and capability flags.
- Public ID, style-color, style-variable, item-width, and disabled-state scopes.
- Window flags and conditions, size constraints, window queries, child regions, scrolling helpers, nested clipping, and `ListClipper`.
- Last-item queries, drag-and-drop sources and targets, and an expanded set of widgets for tool-oriented interfaces.
- Dark, Light, and High Contrast themes plus reduced-motion behavior.
- Per-frame metrics, debug callbacks, state validation, and Metrics Studio.
- JSON state save/load through files and memory, schema version 2 validation, bounded file reads, and `GetLastError()` diagnostics.
- Owned draw-data snapshots for capture and controlled deferred consumption.
- Charts, Markdown rendering hooks, and node-editor facilities in the repository modules.
- CMake package versioning, install/export support, namespaced component targets, installed-package consumer validation, and Windows/Linux CI configurations.

### Changed

- `TextureID` is now a pointer-sized `uintptr_t` value rather than an assumption about 32-bit graphics handles.
- Draw commands now carry index and vertex offsets, logical clip rectangles, effects, optional callbacks, and callback data.
- Draw-list clipping intersects nested rectangles by default and supports texture/effect stacks plus command compaction.
- Character input uses queued/vector storage instead of the earlier fixed/count-style interface.
- CMake consumers can use `OSGui::Core` and optional `OSGui::Examples`, `OSGui::OpenGL3`, `OSGui::Win32OpenGL2`, `OSGui::DX11`, and `OSGui::GLFW` targets.

### Fixed

- Context-bound auxiliary state is isolated per context and released when that context is destroyed.
- Backends account for framebuffer scaling when converting logical clip rectangles to native scissors.
- Maintained renderers handle draw-command callbacks and vertex offsets and clean up backend-owned resources during shutdown.

### Known limitations

- Editable text does not provide IME composition, grapheme-cluster-aware
  movement, bidirectional layout, or full native desktop text-control semantics.
- Complete docking and multi-viewport platform windows are not implemented.
- No Vulkan renderer is included.
- The Win32 GDI font-atlas path is bounded and is not a full Unicode shaping, bidirectional-text, or exhaustive CJK solution.
- OpenGL 2 blur relies on an expensive framebuffer-copy path; OpenGL 3 and DirectX 11 use tint fallback for unsupported effects.
- Test coverage is growing and does not yet establish feature or performance parity with Dear ImGui.
