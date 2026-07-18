# Contributing to OSGui

Thank you for helping improve OSGui. The v2 line is an alpha, so contributions are most useful when they make a narrowly described behavior reliable across the core, diagnostics, tests, documentation, and relevant backends.

Before starting a large API, renderer, docking, viewport, or text-editing change, open an issue that explains the use case and proposed contract. Small fixes, tests, documentation corrections, and contained backend improvements can usually go directly to a pull request.

## Set up a development build

Requirements:

- CMake 3.20 or newer
- a C++11 compiler
- platform SDKs and graphics libraries for any backends you enable

Configure, build, and test from the repository root:

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug -DBUILD_TESTING=ON -DOSGUI_BUILD_TESTS=ON
cmake --build build --config Debug
ctest --test-dir build -C Debug --output-on-failure
```

Also test Release for changes that affect optimization, layout math, memory ownership, rendering, or package exports:

```sh
cmake -S . -B build-release -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=ON -DOSGUI_BUILD_TESTS=ON
cmake --build build-release --config Release
ctest --test-dir build-release -C Release --output-on-failure
```

Enable warnings-as-errors when validating a contribution if your compiler is covered by the project option. Build the relevant example targets for backend changes. CI currently exercises Windows and Linux, Debug and Release, tests, installation, and an installed-package consumer; local platform testing is still required for graphics and input changes.

## Design invariants

Keep these rules intact when adding to the API:

- **Context ownership:** Mutable core state belongs to an `og::Context`. Auxiliary caches must be keyed by context and removed during `DestroyContext()`.
- **Thread model:** Selecting a context is thread-local; mutating one context from multiple threads simultaneously is not supported.
- **Stable identity:** Interactive and persistent state is keyed by IDs, not screen position. Repeated labels need `PushID()` scopes or another stable identifier.
- **Balanced scopes:** Every begin/end and push/pop operation must remain balanced on all control-flow paths. Add validation for new stacks.
- **Logical coordinates:** Core draw data and clip rectangles remain in logical coordinates. Renderers perform framebuffer-scale and API-origin conversion.
- **Backend neutrality:** The core must not depend on Win32, GLFW, DirectX, OpenGL, or another native API.
- **Explicit ownership:** `TextureID` is opaque and pointer-sized. OSGui does not silently take ownership of application graphics resources.
- **Graceful capability fallback:** An unsupported optional effect must degrade visibly and safely, not corrupt renderer state or disappear without explanation.
- **Deterministic frame behavior:** Input helpers update accumulated state and append a diagnostic event record; frame transitions are computed from that state at `NewFrame()`.

Read [docs/architecture.md](docs/architecture.md) before changing context state, the input queue, item behavior, draw commands, persistence, or a backend contract.

## Source changes

- Keep core code compatible with C++11 unless the project version and toolchain policy are deliberately revised.
- Prefer small, composable APIs and keep platform-specific includes outside the core.
- Avoid adding a mandatory dependency to solve an optional module or backend problem.
- Match the surrounding naming, layout, and error-handling style.
- Do not commit generated build directories, local IDE state, caches, or binary artifacts.
- Treat compiler warnings as defects in changed code.
- Explain non-obvious lifetime, coordinate-system, fallback, and state-restoration decisions in code comments.

If a change expands public API, update the header documentation and at least one realistic example or test. Do not advertise a control or backend as complete solely because it compiles.

## Tests

The current suite is a foundation, not exhaustive coverage. Every bug fix should include a regression test when the behavior can be tested without a live graphics device. New core behavior should cover success, invalid input, multiple contexts where relevant, and balanced cleanup.

Useful targets for focused tests include:

- ID stability and isolation between contexts;
- input event recording, focus loss, and key transitions;
- nested clip intersection and framebuffer-scale conversion;
- scope underflow/imbalance diagnostics;
- draw callbacks, index/vertex offsets, command compaction, and snapshots;
- malformed, oversized, older, and round-tripped persistence data;
- disabled, clipped, and keyboard-driven widget behavior;
- installed-package discovery and component combinations.

For visual behavior, include reproducible steps, the backend and graphics API, OS, DPI/framebuffer scale, and before/after captures. A screenshot is supporting evidence, not a substitute for a testable contract.

## Backend contribution checklist

A platform backend should:

- submit pointer, wheel, key, character, and focus changes through the `IO` event helpers;
- use named keys and report modifier transitions consistently;
- publish an accurate backend name and only supported `BackendFlags`;
- release callbacks, cursors, handles, and other backend-owned resources during shutdown;
- avoid preventing the host application from chaining or retaining its native event handling.

A renderer backend should:

- accept pointer-sized `TextureID` values without narrowing;
- convert logical clip rectangles using display position and framebuffer scale, then clamp native scissors;
- honor index and vertex offsets;
- dispatch draw-command callbacks correctly;
- preserve and restore host graphics state affected by rendering;
- document effect support and implement safe fallbacks;
- keep font-atlas and texture lifetimes explicit;
- test empty command lists, minimized/zero-sized framebuffers, high DPI, and shutdown/reinitialization.

State-restoration requirements vary by graphics API. Record exactly which host state the backend saves and restores instead of relying on implicit defaults.

## Documentation and compatibility

Public behavior changes should update all relevant documents:

- [README.md](README.md) for the supported feature and backend matrix;
- [docs/getting-started.md](docs/getting-started.md) for integration-facing changes;
- [docs/migration-v2.md](docs/migration-v2.md) for a breaking change or compatibility trap;
- [CHANGELOG.md](CHANGELOG.md) under `Unreleased`.

Use the real symbol and target names from the checkout. Mark experimental or partial behavior plainly. Avoid claims of Dear ImGui feature or performance parity unless a reproducible, reviewed comparison supports the exact claim.

Persistence changes require a schema decision, validation coverage, an error path, and a migration note. Backend ABI or draw-contract changes require all maintained backends to be updated in the same pull request or explicitly marked incompatible.

## Pull request checklist

Before requesting review:

- [ ] The change has a focused problem statement and no unrelated formatting churn.
- [ ] Debug and Release builds complete on every locally available affected platform.
- [ ] Tests pass, and new behavior or a bug fix has proportionate coverage.
- [ ] Relevant examples/backends were run with the affected DPI and input paths.
- [ ] New scopes, allocations, callbacks, and native resources clean up on error and shutdown.
- [ ] Public API and package changes are reflected in documentation and `CHANGELOG.md`.
- [ ] Known limitations and capability fallbacks are stated honestly.
- [ ] No generated artifacts, credentials, private paths, or unrelated local files are included.

In the pull request description, include the exact configure/build/test commands used and note anything you could not verify locally.
