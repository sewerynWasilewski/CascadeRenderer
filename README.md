# Cascade Renderer

A backend-agnostic, data-oriented 3D renderer built around a render graph architecture.

Passes declare resource reads and writes. The graph resolves dependencies, generates synchronization barriers, manages transient resource lifetimes, and dispatches work to a pluggable graphics backend.

---

## Design Goals

- **Render Graph core is backend-agnostic** — no Vulkan or OpenGL headers inside `src/rg/`
- **Data-Oriented** — passes, resources, usages, edges, and barriers stored in flat arrays
- **C-style API surface** — core exposed as plain structs and free functions, C++ used only for ergonomics
- **Explicit synchronization** — barriers generated from declared usage transitions, not guessed

---

## Implementation Stages

- [ ] **Stage 1** — Graph core: passes, resources, dependency edges, topological sort
- [ ] **Stage 2** — Barrier generation: usage transition analysis, RHI barrier emission
- [ ] **Stage 3** — Transient resources: lifetime tracking, memory aliasing
- [ ] **Stage 4** — Vulkan backend: device init, swapchain, dynamic rendering
- [ ] **Stage 5** — Debug tools: graph visualization, resource lifetime view, pass toggle
