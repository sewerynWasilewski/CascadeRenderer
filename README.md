# Cascade Renderer

A backend-agnostic, data-oriented 3D renderer built around a render graph architecture.

Passes declare resource reads and writes. The graph resolves dependencies, generates synchronization barriers, manages transient resource lifetimes, and dispatches work to a pluggable graphics backend.

---

## Design Goals

- **Render Graph core is backend-agnostic** - no Vulkan or OpenGL headers inside `src/rg/`
- **Data-Oriented** - passes, resources, usages, edges, and barriers stored in flat arrays
- **C-style API surface** - core exposed as plain structs and free functions, C++ used only for ergonomics
- **Explicit synchronization** - barriers generated from declared usage transitions, not guessed

---

## Building

**Requirements:** CMake 3.24+, C++20 compiler. [Vulkan SDK](https://vulkan.lunarg.com) is optional - without it the Vulkan backend is excluded but the sandbox still builds.

```bash
cmake -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build
./build/sandbox/sandbox
```

## Testing

Tests use [doctest](https://github.com/doctest/doctest) (vendored, no install needed). They cover render graph correctness - pass ordering, edge generation, barrier emission - and run against a `MockBackend` with no GPU required.

```bash
cmake --build build
ctest --test-dir build --output-on-failure

# or directly for verbose per-test output:
./build/tests/rg_tests
```

Tests are also run automatically on every push via GitHub Actions.

Current tests inspect internal state (`mSortedPasses`, `mEdges`, `mBarriers`) via accessors gated behind `RG_ENABLE_TESTS`. This is a temporary approach - once `execute()` is implemented the tests will be rewritten to observe callback execution order instead, and the internal accessors removed.

---

## Implementation Stages

- [ ] **Stage 1** - Graph core: passes, resources, dependency edges, topological sort
- [ ] **Stage 2** - Barrier generation: usage transition analysis, RHI barrier emission
- [ ] **Stage 3** - Transient resources: lifetime tracking, memory aliasing
- [ ] **Stage 4** - Vulkan backend: device init, swapchain, dynamic rendering
- [ ] **Stage 5** - Debug tools: graph visualization, resource lifetime view, pass toggle
