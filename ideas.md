# Ideas to implement

1. Let Developer reserve size of passes/barriers/resources upfront (number of expected types in vectors), helps with CPU side memory allocation
2. Should RGsetBackend() trigger cleanup of resources for previous backend so there is no memory leak on GPU?

3. RenderGraphDesc config struct + auto-shrink flag for transient memory pools
   - `RenderGraphDesc { bool autoShrink = false; u32 shrinkThresholdFrames = 60; f32 shrinkRatio = 2.0f; }`
   - passed to `RenderGraph::init(const RenderGraphDesc&)`
   - auto-shrink fires in `allocate()` when: `capacity > shrinkRatio * totalBytes` for N consecutive frames, then shrinks to `totalBytes * 1.5` (with slack)
   - `trim()` as a separate explicit method, independent of the flag — call on loading screen / level transition
   - Analogous to UE5 CVars for pool trimming and Frostbite GC aggressiveness modes

5. Naming consistency fixes (from audit 2026-10-03)
   - Critical: move `RG_INVALID_ID` from `types.h` to `RGTypes.h` (wrong layer prefix)
   - Critical: rename `AcquiredHandle` → `RGAcquiredHandle` or make it private to RGResourceHandler
   - Minor: remove dead `rg_suballocate()` from `RGData.h` (allocate() doesn't use it)
   - Minor: `VkRHIBackend/Allocator` param `gpuHandle` → `handle` (conflicts with RHI rename intent)
   - Minor: `mock_backend.h` uses `uint32_t` instead of `u32`
   - Minor: `RGPassCallback` is struct with `mExecFunc` member — use class or drop m prefix
   - Trivial: `MockBackend`/`MockTexture` duplicated in sandbox/main.cpp and tests/mock_backend.h

6. GPU pass profiler via VkQueryPool timestamps
   - `VK_QUERY_TYPE_TIMESTAMP` pair (begin/end) per pass in execute()
   - guarded by `RG_ENABLE_PROFILING` compile flag - zero cost in release
   - read back via `vkGetQueryPoolResults` after frame
   - accumulate N-frame history for bar chart (per-pass timing like depthPrePass, gBufferPass etc.)
   - hook into existing beginPass()/endPass() calls in execute()

7. Data-driven render graph via JSON + UI editor
   - JSON defines graph topology: pass names, resource declarations, read/write edges, flags
   - code side: named pass registry - `PassRegistry::register("GBufferPass", execCallback)`
   - JSON loader calls addPass() with setup from JSON and exec from registry
   - enables UI editor (node graph) for modifying RG topology without code changes
   - target use case: non-programmer can adjust pass order, toggle passes, change resource formats
   - similar to UE5 material graph / Blender shader nodes for render pipeline
   - limitation: execute callbacks still code; only topology is data-driven

4. RG_ASSERT macro for modulatable assert behavior
   - `#define RG_ASSERT(cond) assert(cond)` initially
   - future: swap to custom handler (log + continue in release, crash in debug), breakpoint trap, etc.
   - avoids touching every assert site when behavior needs to change

8. Renderer facade architecture decisions (2026-11-06 session, Vulkan-only for now)
   - `Renderer` owns `std::unique_ptr<IRHIBackend>` + `RenderGraph`, wires them via `setBackend()`
   - Backend selected at compile time via `RENDERER_VULKAN` (already set by CMakeLists.txt when Vulkan_FOUND) - `Renderer::init()` is the ONLY place that names `VkRHIBackend` concretely
   - Escape hatch for native interop: `IRHIBackend::getNativeHandles() -> const void*`; each backend defines its own native-handles struct in its own header (e.g. `VkRHINativeHandles` in `VkRHIBackend.h`) - no shared/aliased "spec" base across backends, because there are no common fields to factor out and C++ construction isn't polymorphic anyway, so a shared base buys nothing
   - Same reasoning applies to future per-backend init config (`VkRHIBackendConfig` for extra instance/device extensions): no shared base, add it only when something concrete needs an extra extension, not preemptively
   - `createSwapChain`/`beginFrame`/`endFrame`/`getCurrentCommandBuffer`/`getCurrentSwapchainTarget`/`getSwapchainExtent` live on `IRHIBackend` itself (void*/u32-based, no Vulkan types) since every backend needs equivalent operations
   - ImGui/debug-UI setup lives OUTSIDE Renderer entirely (`sandbox/debug_ui.h/.cpp`), built purely on Renderer's public escape hatches - Renderer never knows ImGui exists. Mirrors bgfx's own ImGui helper, which is built on bgfx's public API rather than baked into bgfx itself
   - Window/surface creation stays Vulkan-aware in `sandbox/main.cpp` (GLFW native window -> `VkSurfaceKHR`) - deliberately not hidden; this is "platform layer" code, same split real engines (UE5, bgfx) use between RHI and platform integration
   - Deferred until a second backend actually exists: multi-backend `Renderer::setBackend(enum)` runtime factory + `IDebugOverlay` abstraction for picking the right ImGui backend - building this now would mean guessing what a second backend needs before one exists
   - UPDATE 2026-11-07: this last bullet is done - `Renderer::init(RHIBackendType type)` now takes the backend as a runtime enum, dispatched via `switch`, not `#ifdef`-only. `RENDERER_VULKAN` etc. still gate which `case`s even exist in the switch (compile-time: is this backend's code present in this build), but choosing among the compiled-in ones is now a runtime decision

9. Multi-backend platform/loading plan (2026-11-07 session)
   - `RHIBackendType` enum always declares ALL values (Vulkan/Metal/D3D12/OpenGL) on every platform - it's just a named constant, costs nothing to declare
   - Per-platform `#ifdef` in `Renderer::init()`'s switch decides which `case`s exist for a given build (e.g. no `RHIBackendType::D3D12` case on a macOS build) - requesting an unavailable-for-this-build backend falls to `default:` and throws a clean runtime error, since backend choice is runtime data (config/cmdline/settings), not something a compile-time check could validate anyway
   - D3D12 (`d3d12.dll`) and Metal (`Metal.framework`) ship as part of the OS itself - guaranteed present on a supported OS version, so they can link directly with no dlopen-style loading
   - Vulkan and OpenGL are driver-vendor-supplied, NOT guaranteed by the OS - these need dynamic loading (`volk` for Vulkan, GLAD/GLEW/epoxy for OpenGL - this is actually how 100% of OpenGL code already works, nothing OpenGL-specific to invent) so a missing driver produces a clean runtime error instead of the whole process refusing to launch
   - Checked: `IRHIAllocator`/`VkRHIAllocator` already follow this same separation correctly - `IRHIAllocator` is RHIMemoryBlock/RHIMemoryType only, all VMA/`VkDeviceMemory` specifics are private to `VkRHIAllocator`. A future Metal backend's allocator (e.g. around `MTLHeap`) needs zero changes to the shared contract
   - DONE 2026-11-09: `volk` implemented for the Vulkan backend. Vendored as `vendor/volk` submodule, compiled as a plain source file (not a lib). `VkRHIBackend`/`VkRHIAllocator.h` include `<volk.h>` instead of `<vulkan/vulkan.h>`; `volkInitialize()` runs first in the constructor (throws a clean error if the loader isn't present instead of crashing at launch), `volkLoadInstance()`/`volkLoadDevice()` run right after each is created. Dropped `target_link_libraries(renderer PUBLIC Vulkan::Vulkan)` entirely - only `Vulkan_INCLUDE_DIRS` remains (headers only, no link). Per-platform `VK_USE_PLATFORM_*` compile defs replace the old manual `#ifdef`+`#include <vulkan/vulkan_*.h>` block - volk.h pulls in the right platform surface header itself once that macro is set
   - Two integration gotchas worth remembering: (1) ImGui's Vulkan backend has official volk support via `IMGUI_IMPL_VULKAN_USE_VOLK` - no changes needed to call sites. (2) VMA defaults to `VMA_DYNAMIC_VULKAN_FUNCTIONS` whenever `VK_NO_PROTOTYPES` is set (which volk sets) - needs VMA's own `vmaImportVulkanFunctionsFromVolk()` helper to populate `VmaAllocatorCreateInfo::pVulkanFunctions`, asserts at runtime otherwise
   - Include order matters: volk.h must be included before anything else in a TU that transitively pulls in raw `vulkan.h` (e.g. GLFW's `GLFW_INCLUDE_VULKAN`), otherwise you get "redefinition as different kind of symbol" - real vs volk's function-pointer-shaped declarations collide