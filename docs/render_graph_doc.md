# Render Graph Implementation — Backend-Agnostic, DOD-Oriented C/C++ Design

## 1. Goal

The goal of the project is to implement a **Render Graph** system that organizes rendering work as a graph of passes and resources. The system should automatically track resource usage, build dependencies between passes, generate synchronization barriers, manage transient resources, and execute rendering commands through different graphics backends such as **Vulkan**, **OpenGL**, or future APIs.

The design should be:

- **backend-agnostic** — Render Graph logic should not depend directly on Vulkan/OpenGL details,
- **Data-Oriented** — internal data should be stored in arrays and compact structures for better cache usage,
- **usable from C** — the core API can be exposed as pure C,
- **comfortable in C++** — C++ can provide wrappers, RAII, templates, and stronger type safety.

---

## 2. High-Level Architecture

The renderer can be split into several layers:

```text
Application / Sandbox
        |
        v
Renderer / Render Context
        |
        v
Render Graph Core
        |
        v
Backend Interface
        |
        v
Vulkan Backend / OpenGL Backend / Future Backend
```

### Responsibilities

#### Application / Sandbox

The sandbox is used to test and demonstrate the renderer. It can load models, textures, shaders, and allow the user to dynamically enable or disable render passes.

Example features:

- model loading,
- camera movement,
- basic scene setup,
- debug UI,
- visualization of graph passes and resources,
- enabling/disabling passes.

#### Renderer

The renderer owns higher-level rendering logic. It decides which render passes should exist in the frame.

Examples:

- geometry pass,
- shadow pass,
- lighting pass,
- post-processing pass,
- UI pass.

The renderer does not directly manage low-level barriers or transient resources. It describes rendering work to the Render Graph.

#### Render Graph Core

The Render Graph is responsible for:

- registering passes,
- registering resources,
- tracking reads and writes,
- building dependencies,
- sorting passes topologically,
- detecting hazards,
- generating barriers,
- calculating resource lifetimes,
- allocating transient resources,
- executing passes in the correct order.

#### Backend Interface

The backend interface hides API-specific details. The Render Graph emits generic operations such as:

- create texture,
- create buffer,
- begin render pass,
- bind pipeline,
- emit barrier,
- execute draw command.

The concrete backend translates these operations into Vulkan, OpenGL, or another graphics API.

---

## 3. Basic Render Graph Concepts

### Render Pass

A render pass is a unit of work in the graph.

Example:

```text
GBuffer Pass
Reads: scene buffers, material textures
Writes: gbuffer_albedo, gbuffer_normal, depth
```

Another example:

```text
Lighting Pass
Reads: gbuffer_albedo, gbuffer_normal, depth
Writes: hdr_color
```

Each pass declares what resources it reads and writes. Based on this information, the graph can determine ordering and synchronization.

### Resource

A resource is a texture or buffer used by passes.

Examples:

- color texture,
- depth texture,
- shadow map,
- uniform buffer,
- storage buffer,
- vertex/index buffer.

Resources can be:

- **external** — owned outside the graph, for example the swapchain image,
- **transient** — created and destroyed automatically by the graph,
- **persistent** — reused between frames.

### Usage

Every pass declares usage of a resource.

Example usages:

```c
RG_USAGE_COLOR_ATTACHMENT
RG_USAGE_DEPTH_ATTACHMENT
RG_USAGE_SAMPLED_TEXTURE
RG_USAGE_STORAGE_TEXTURE
RG_USAGE_TRANSFER_SRC
RG_USAGE_TRANSFER_DST
RG_USAGE_VERTEX_BUFFER
RG_USAGE_INDEX_BUFFER
RG_USAGE_UNIFORM_BUFFER
```

The graph uses these declarations to generate proper synchronization barriers.

---

## 4. Backend-Agnostic Design

The Render Graph should not directly call Vulkan or OpenGL functions. Instead, it should communicate with a backend through a small interface.

### Generic Backend Interface

Example C-style interface:

```c
typedef struct RHI_Backend RHI_Backend;

typedef struct RHI_TextureHandle {
    uint32_t id;
} RHI_TextureHandle;

typedef struct RHI_BufferHandle {
    uint32_t id;
} RHI_BufferHandle;

typedef struct RHI_TextureDesc {
    uint32_t width;
    uint32_t height;
    uint32_t mip_count;
    uint32_t format;
    uint32_t usage_flags;
} RHI_TextureDesc;

typedef struct RHI_BufferDesc {
    uint64_t size;
    uint32_t usage_flags;
} RHI_BufferDesc;

typedef struct RHI_BarrierInfo {
    uint32_t resource_type;
    uint32_t resource_id;
    uint32_t old_usage;
    uint32_t new_usage;
    uint32_t src_pass;
    uint32_t dst_pass;
} RHI_BarrierInfo;

typedef struct RHI_BackendVTable {
    RHI_TextureHandle (*create_texture)(RHI_Backend*, const RHI_TextureDesc*);
    RHI_BufferHandle  (*create_buffer)(RHI_Backend*, const RHI_BufferDesc*);

    void (*destroy_texture)(RHI_Backend*, RHI_TextureHandle);
    void (*destroy_buffer)(RHI_Backend*, RHI_BufferHandle);

    void (*emit_barrier)(RHI_Backend*, const RHI_BarrierInfo*);
    void (*begin_pass)(RHI_Backend*, uint32_t pass_id);
    void (*end_pass)(RHI_Backend*, uint32_t pass_id);
} RHI_BackendVTable;

struct RHI_Backend {
    void* user_data;
    RHI_BackendVTable* vtable;
};
```

The Render Graph only sees `RHI_Backend`. It does not care whether the implementation is Vulkan, OpenGL, or something else.

---

## 5. Vulkan Backend

The Vulkan backend translates generic Render Graph operations into Vulkan API calls.

It is responsible for:

- creating `VkImage` and `VkBuffer`,
- managing `VkImageView`, `VkSampler`, `VkDeviceMemory`,
- translating generic usages into Vulkan layouts and access masks,
- emitting `vkCmdPipelineBarrier2`,
- beginning and ending rendering with dynamic rendering or render passes,
- managing command buffers.

Example usage translation:

```text
RG_USAGE_COLOR_ATTACHMENT
    -> VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL
    -> VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT
    -> VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT

RG_USAGE_SAMPLED_TEXTURE
    -> VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
    -> VK_ACCESS_2_SHADER_SAMPLED_READ_BIT
    -> VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT
```

The Render Graph should not know about these Vulkan-specific values. The Vulkan backend performs the translation.

---

## 6. OpenGL Backend

OpenGL has a different synchronization model than Vulkan. It does not require explicit layout transitions in the same way.

The OpenGL backend can implement the same interface, but many barriers may become simplified or no-op operations.

Example:

```c
void opengl_emit_barrier(RHI_Backend* backend, const RHI_BarrierInfo* barrier) {
    if (barrier->new_usage == RG_USAGE_STORAGE_TEXTURE) {
        glMemoryBarrier(GL_SHADER_IMAGE_ACCESS_BARRIER_BIT);
    }
}
```

This means the same Render Graph can run on OpenGL, even if the backend internally handles synchronization differently.

---

## 7. Data-Oriented Design

The Render Graph should avoid object-heavy designs where every pass or resource is a separate heap allocation. Instead, it should store data in compact arrays.

### Recommended Layout

```c
typedef struct RG_Pass {
    uint32_t name_id;
    uint32_t first_read;
    uint32_t read_count;
    uint32_t first_write;
    uint32_t write_count;
    uint32_t execute_callback_id;
} RG_Pass;

typedef struct RG_Resource {
    uint32_t name_id;
    uint32_t type;
    uint32_t desc_index;
    uint32_t first_usage;
    uint32_t last_usage;
    uint32_t physical_handle;
} RG_Resource;

typedef struct RG_ResourceUsage {
    uint32_t pass_id;
    uint32_t resource_id;
    uint32_t usage;
    uint32_t access_type;
} RG_ResourceUsage;
```

Instead of storing vectors inside every pass, the graph stores ranges into larger arrays.

```text
passes[]
resources[]
resource_usages[]
edges[]
barriers[]
```

This improves cache locality and makes graph compilation faster.

---

## 8. Dependency Graph

Dependencies are built from resource usage.

If one pass writes a resource and another pass reads it, the second pass depends on the first.

Example:

```text
Pass A writes hdr_color
Pass B reads hdr_color

A -> B
```

The graph stores dependencies as edges:

```c
typedef struct RG_Edge {
    uint32_t from_pass;
    uint32_t to_pass;
    uint32_t resource_id;
} RG_Edge;
```

After building edges, the graph performs a topological sort to find the correct execution order.

---

## 9. Barrier Generation

Barriers are generated by comparing previous and next usages of the same resource.

Example:

```text
Pass A writes color attachment
Pass B samples the texture
```

This creates a transition:

```text
COLOR_ATTACHMENT_WRITE -> SHADER_READ
```

Generic barrier:

```c
typedef struct RG_Barrier {
    uint32_t resource_id;
    uint32_t before_usage;
    uint32_t after_usage;
    uint32_t src_pass;
    uint32_t dst_pass;
} RG_Barrier;
```

The backend receives this generic barrier and translates it to API-specific synchronization.

---

## 10. Transient Resources and Lifetime Tracking

The Render Graph can optimize memory by tracking when resources are first and last used.

Example:

```text
gbuffer_normal: first used in GBuffer Pass, last used in Lighting Pass
bloom_temp: first used in Bloom Downsample, last used in Bloom Upsample
```

If two resources are not alive at the same time, they can share memory.

```text
Resource A lifetime: Pass 0 -> Pass 2
Resource B lifetime: Pass 4 -> Pass 6

They can alias the same memory block.
```

This is especially useful in Vulkan where transient images can consume a lot of GPU memory.

---

## 11. Command Pattern

Passes should not immediately execute rendering commands during graph construction. Instead, they should record commands or provide an execute callback.

Example:

```c
typedef void (*RG_ExecuteCallback)(RHI_Backend* backend, void* user_data);

typedef struct RG_PassExecuteData {
    RG_ExecuteCallback callback;
    void* user_data;
} RG_PassExecuteData;
```

During graph execution:

```c
for each pass in sorted_passes:
    emit barriers for pass
    backend->begin_pass()
    execute pass callback
    backend->end_pass()
```

This separates declaration from execution.

---

## 12. Pure C Core

The core Render Graph can be implemented in pure C style.

Benefits:

- easy ABI boundary,
- possible use from C or C++,
- simpler backend plugin system,
- easier debugging,
- predictable memory layout.

---

## 13. C++ Wrapper Over C Core

C++ can be used as a safer and more ergonomic layer above the C core.

C++ can provide:

- RAII lifetime management,
- type-safe handles,
- templates for pass data,
- lambdas for pass execution,
- easier integration with engine systems.

---

## 14. Suggested Implementation Stages

### Stage 1 — Basic Graph

- define pass API,
- add passes to graph,
- register texture and buffer resources,
- declare read/write usages,
- build dependency graph,
- topological sort,
- execute passes in sorted order.

### Stage 2 — Synchronization

- analyze read/write hazards,
- generate generic barriers,
- translate generic barriers to Vulkan barriers.

### Stage 3 — Transient Resources

- track first and last usage of resources,
- allocate transient resources only when needed,
- reuse compatible memory.

### Stage 4 — Backend Abstraction

- clean `RHI_Backend` interface,
- Vulkan backend implementation,
- optional OpenGL backend implementation.

### Stage 5 — Debugging and Sandbox

- graph visualization,
- resource lifetime view,
- barrier debug output,
- pass enable/disable options.

---

## 15. Important Design Rules

1. The Render Graph should describe rendering work, not directly execute API calls during setup.
2. Passes should declare resource usage explicitly.
3. The graph should own dependency analysis.
4. The backend should own API-specific details.
5. Vulkan-specific types should not leak into the graph core.
6. Store passes, resources, usages, edges, and barriers in arrays.
7. Prefer handles and indices over pointers.
8. Keep the C core simple and predictable.
9. Use C++ only where it improves safety and usability.
