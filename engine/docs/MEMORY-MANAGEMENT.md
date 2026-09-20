# Renderer memory management

MAKO Renderer allocates private Vulkan resources when a swapchain or processing context is constructed, reuses them during presentation, and releases them after the owning GPU work completes. Live resource changes can temporarily keep both the active and replacement contexts allocated. Frame Generation Off retains provisioned resources for live reactivation, and the layer deliberately retains its private backend Vulkan device until process exit.

This guide owns the allocation, ownership, and accounting overview. [Spatial scaling](SCALING.md#extent-policy) owns memory-admission policy, [runtime transitions](RUNTIME-TRANSITIONS.md) owns replacement decisions, [lifecycle](LIFECYCLE.md#destruction-and-retirement) owns swapchain retirement, and [HDR](HDR-PIPELINE.md) owns transport and working-image formats. Those policies remain authoritative for their thresholds and compatibility rules.

## Resource owners

| Resource | Owner and lifetime |
| --- | --- |
| Application device and lower WSI swapchain images | The application and Vulkan WSI own their creation/destruction contract. MAKO may defer lower-swapchain destruction under the retirement policy; these image allocations are outside its private memory counters. |
| Exported real-frame and generated-output images, shared synchronization, private scaler, and presentation resources | One `Swapchain` in `mako-render/src/swapchain/`; a private replacement may retire a subset before the game recreates its swapchain. |
| Imported exchange images, model intermediates, constant buffers, and backend command resources | A backend `ContextImpl` in `mako-backend/src/mako.cpp`; closed through `backend::Instance` after its work completes, or retained for later retirement. |
| Internal model-image blocks | One `vk::ImageMemoryPool` per backend context, shared by the LSFG shader chains that explicitly request pooled images. Bound images also retain their block owner. |
| Backend Vulkan device and shader registry | The lazily constructed process-wide backend instance. The layer enables the device-retention exception described below. |
| Host containers and setup data | Their C++ owners. These allocations are not counted by Vulkan device-memory accounting. Licensed model inputs and translated resources remain subject to the process-local boundary in the [Renderer README](../README.md). |

The application-facing device and private backend device can differ. Export/import connects their exchange images; it does not create two independent copies of the same image payload. A source, format, or synchronization change must preserve that cross-device contract.

## Allocation paths

`mako-common` owns the Vulkan wrappers. `Vulkan::findMemoryTypeIndex()` selects the first compatible memory type with the requested properties: device-local for images, or host-visible and host-coherent for buffers. Selection uses the resource's Vulkan `memoryTypeBits`; it does not classify GPUs by name or treat system RAM as an interchangeable allocation target.

| Path | Allocation behavior |
| --- | --- |
| Internal image with an explicit pool | Bind a unique aligned range from a context-local block. The image retains shared ownership of that block. |
| Internal image without a pool | Allocate a separate `VkDeviceMemory` handle using the image's reported requirements and bind at offset zero. Spatial-scaler images use this path. |
| Exported or imported image | Use a separate allocation with the external-memory and dedicated-image allocation structures. External images cannot use the internal pool. |
| Buffer | Allocate a separate host-visible, host-coherent memory handle, then map/copy/unmap for upload or readback. Buffers do not use the image pool. |

Allocation or binding failure propagates to the owning construction path. A successful allocation call returning a null memory handle is also rejected. The allocator does not evict another context, alias live images, or move a failed device-local request into a different storage policy.

### Internal image pooling

`ImageMemoryPool` reduces separate Vulkan allocation calls for the many LSFG model intermediates. Each block has one memory type and a linear cursor. Binding searches existing blocks of the selected type, aligns the cursor to the image requirement, and reserves a non-overlapping range. The cursor rejects invalid alignment, arithmetic overflow, and requests that do not fit without changing its position.

The default minimum block size is 4 MiB. With that default, a request up to 8 MiB gets a new block sized to the larger of 4 MiB or four times its requirement. Requests above 8 MiB through 32 MiB get a 32 MiB block; a single image larger than 32 MiB gets a block sized to its own requirement. The final block size is aligned to the image requirement. These are block-sizing rules, not a total context or device-memory cap.

There is no free-list, compaction, or individual range reuse. Destroying an image does not rewind the cursor. A failed Vulkan bind after range reservation also leaves that range consumed until the block is released. The pool retains its blocks, and each successfully bound image retains its block independently; a block is freed only when the pool and all image references to it are gone. This favors context-wide construction and retirement rather than repeated individual image allocation/deallocation.

Pooling is opt-in, not a global allocator for all Renderer images. The LSFG shader chains use the context pool, while exchange images, host-visible buffers, spatial-scaler images, and other images constructed without a pool keep their separate allocations. Pooling reduces allocation count; it does not reduce image dimensions or alias model intermediates. Unused block capacity and alignment padding remain allocated and appear in memory accounting.

### Model scratch lifetimes

The model graph reuses existing same-format, same-extent images only after their earlier contents have reached their last reader. Gamma1 borrows Alpha0's output images after Alpha1 consumes them during the prepass. Delta1 already borrows Gamma1's second scratch set; after its first dispatch consumes Gamma0's outputs, it also reuses those outputs and the unused tail of Gamma1's first scratch set. Gamma1's first `m` images remain intact until Delta1's second half consumes them. `helpers/delta_scratch.hpp` owns that split for both model sizes and rejects incomplete or overlapping input sets.

These are sequential uses of the same `VkImage`, not overlapping bindings of different images to the same memory range. Alpha0 and Gamma0/Gamma1 retain ownership, and each image is initialized once by its owner. The prepass semaphore orders generation after Alpha1's reads, existing per-image compute barriers order successive scratch reads/writes across generated passes, and the context completion fence keeps the next prepass and resource retirement behind all generated work. Those dependencies must remain intact if recording, submission or concurrency changes. Temporal quality and synchronization validation must cover changing output counts, history-only steps and repeated source/history phases.

### Recorded model commands

Each context lazily records its prepass and generated-output commands for each combined source/history phase. `helpers/temporal_phases.hpp` owns Alpha1's history counts and derives the six-phase recording period from those counts and the two source-image slots. All missing generated recordings are completed before the prepass is submitted, so a cold-cache allocation or recording failure cannot leave partial GPU work behind. The previous completed fence stays intact during recording; immediately before the first submission it is reset and the context is marked as submitted, preserving retirement protection if a later submission fails. Reuse preserves shaders, descriptors, dispatches, barriers, timeline values and completion-fence placement. Timestamp changes still update each output's uniform buffer only after previous work completes; they do not change recorded descriptor bindings. Context replacement creates a new recording set, and retirement retains old commands with their images until work completes. Retaining recordings trades a small driver command-memory allocation for lower CPU recording cost; image reuse and command-memory costs must be measured together.

## Ownership and cleanup

The wrappers use C++ ownership to release Vulkan handles. `vk::Image` declares memory before image and image-view members so normal destruction releases the view, then the image, then its allocation or pooled-block reference. `vk::Buffer` similarly releases the buffer before its memory. `ls::owned_ptr` supplies the move-only handle owner and custom deleter; it is not a GPU-completion mechanism. The owning Vulkan device must outlive every wrapper that calls its destruction functions.

`vk::Shader` releases its input shader module after compute-pipeline creation succeeds. The compiled pipeline, layouts and model registry remain available for context reuse and live mode changes. Vulkan explicitly permits [destroying shader modules while their pipelines remain in use](https://docs.vulkan.org/refpages/latest/refpages/source/vkDestroyShaderModule.html); failure during construction still unwinds through the same handle owners.

`Swapchain::FrameGenerationResources` keeps source images, destination images, and the shared semaphore together with the backend-context owner. Its declaration order closes the context before releasing the exported resources it imports. Live handoff first establishes that the old backend and application-device work are idle, then moves the whole resource group. Preserve this order when adding members or changing replacement code.

External file descriptors have a separate ownership transition. Export returns an owned descriptor only after image-view construction succeeds. The backend context call consumes the whole descriptor batch on entry; unattempted imports remain scoped for cleanup if construction fails. Successful Vulkan import transfers descriptor ownership to Vulkan, so a later bind or view failure must not close that descriptor number again. The detailed failure contract and tests are in [external descriptor ownership](RUNTIME-TRANSITIONS.md#external-descriptor-ownership-during-resource-construction).

## Admission and peak memory

Scaling admission samples the selected application's device-local heap and, when available, live budget/usage before accepting a resource envelope. It reserves non-MAKO headroom and can reduce the effective scale or leave scaling inactive. [Extent policy](SCALING.md#extent-policy) defines the static fallback, graph estimates, pre/post-Frame Generation placement, generated-output allowance, and conditions for reusing a proven envelope.

Admission is an estimate, not an allocation reservation or a performance guarantee. Driver requirements, block padding, other processes, and the separate backend device can affect actual allocation success. It is not a continuously enforced global memory cap. MAKO samples memory budget at admission boundaries rather than polling driver-wide memory pressure on a background timer. Automatic performance recovery therefore starts in place while spatial scaling is active. One later game-owned recreation is allowed only after the event-backed deficit persists for three seconds after that attempt, and never when create-time scaling admission was memory-constrained; this prevents an already known pressured heap from entering the complete scaled-context rebuild.

Steady-state usage and replacement peaks answer different questions. A live Flow Scale, model, capacity, or scaler change constructs a complete candidate while the current resources remain usable. Until drain and handoff finish, both sets contribute to the live counters. Deferred backend contexts and lower WSI swapchains can extend that overlap. Do not interpret the cold graph estimate as an exact prediction of this transient peak.

Frame Generation Off stops generation work while retaining resources when provisioning succeeded, so turning it off is not a request to reclaim its memory. A scaling-only process that never created a Frame Generation backend does not have that same retained FG allocation. FP16 arithmetic also does not imply that SDR image allocation sizes are halved; image formats, geometry, graph selection, and output capacity determine the allocation demand.

## Replacement, retirement, and process exit

Private replacement follows the existing prepare/drain/commit coordinator. Preparation failure discards the candidate, keeps the active resources, and schedules a retry. A superseded candidate is discarded. Completion checks concern MAKO-owned work; private transitions do not introduce a device-wide `vkDeviceWaitIdle`. See the [private replacement contract](RUNTIME-TRANSITIONS.md#private-replacement-contract) for the complete policy.

Backend context close waits up to 250 ms for that context's work. If completion cannot be established, it moves the context to a retirement list. Subsequent context opens/closes poll that list without blocking and reclaim completed entries. After collection, opening another context is rejected when at least two contexts remain retired. This is an admission guard against accumulating unresolved work, not proof that a timed-out context is safe to free.

Lower WSI swapchain retirement is separate. Maintenance fences, same-surface replacement progress, compositor grace, custom allocator callbacks, and terminal surface/device destruction determine when those handles can be released. The [lifecycle destruction policy](LIFECYCLE.md#destruction-and-retirement) owns those conditions; backend completion alone does not prove presentation retirement.

The optional `GamescopeScalingSurface` connection is owned by Renderer instance state only when Scaling needs an isolated Gamescope X11-to-Wayland association. Each Vulkan surface owns one Wayland surface and one association object; lower Vulkan destruction and same-surface retirement complete before these are released. An association is bound on first present so preparing a replacement cannot retire the current window image early. The connection and client libraries outlive all of these objects and are released after the final Vulkan instance. This is CPU-side surface bookkeeping outside the private GPU allocation counters, with no timing history or additional worker thread.

The layer calls `backend::makeLeaking()` after provisioning Frame Generation to preserve the private backend Vulkan owner during teardown and avoid loader-destruction hazards in mixed layer stacks. The backend instance destructor moves that Vulkan owner into deliberately retained storage. The operating system reclaims it when the process exits. This exception is distinct from ordinary context retirement and does not make all per-context images permanent. Standalone backend consumers do not enable it merely by constructing an instance.

## Reading memory evidence

`DeviceMemoryAccounting` tracks `VkDeviceMemory` allocations made through MAKO's wrappers and image pools. Each `vk::Vulkan` owner has its accounting state; application-device and backend-device records must be read with their device/context scope in mind.

| Counter category | Meaning |
| --- | --- |
| `internal` | Non-external allocations, including full pool blocks and host-visible buffers. This is not exclusively VRAM. |
| `exported` | MAKO allocations exported for sharing with another Vulkan device. |
| `imported` | Imported memory handles and their mapped sizes. These are tracked separately because their backing storage has already been allocated by the exporter. |
| `peak*` | Historical high-water marks for live bytes and allocation count in each category. Freeing resources reduces current totals, not these peaks. |

Count each shared payload once when estimating MAKO-owned memory: combine the relevant internal and exported allocations, and report imported mappings separately. Adding imported bytes to the corresponding export double-counts that payload. A pool block counts as one allocation regardless of how many images use it.

These counters do not measure total process RSS, all driver allocations, physical residency, WSI image storage, or exact free GPU memory. Per-field atomic snapshots are observations, not an atomic transaction across every counter; byte and allocation peaks can occur at different times. Context creation deltas are most useful at controlled construction boundaries.

Existing records include:

- `MAKO Renderer: backend-memory operation=context-open`: context allocation deltas, model/precision, output count, and backend live/peak internal totals.
- `MAKO Renderer: renderer-memory operation=swapchain-context-create`: application-side allocation deltas, context and layer role, extents, and live/peak totals.
- `MAKO Renderer: renderer-memory operation=private-context-prepared` and `operation=private-context-applied`: application-side totals at Frame Generation replacement boundaries.

Use [standalone diagnostics](COLLECT_DIAGNOSTICS.md) or the [MAKO Decky collection guide](../../plugin/docs/COLLECT_DIAGNOSTICS.md) to retain these records with transition and retirement evidence. Compare repeated equivalent states after warmup and completed retirement. A temporary overlap, pool slack, retained Off state, or unchanged high-water mark alone is not evidence of a leak. Repeated growth in settled live allocations, descriptor counts, or process memory needs investigation against the owning lifetime.

## Validation and change ownership

[Testing MAKO](../../TESTING.md) owns the required portable gates and hardware-suite selection. Full Renderer CTest includes the following focused coverage:

| Test | Evidence |
| --- | --- |
| `image-memory-pool` | Linear-cursor alignment, capacity rejection, and invalid-alignment behavior; it does not exercise real Vulkan block allocation/binding. |
| `device-memory-accounting` | Separate ownership categories, balanced allocation/free totals, and retained high-water marks. |
| `external-fd-ownership` | Partial export/import failures and descriptor-number reuse. |
| `profile-update`, `runtime-transition`, `spatial-scaling-policy`, `swapchain-retirement` | Deterministic policy and lifetime decisions, including admission and replacement boundaries. |
| `backend-hot-path`, `command-buffer-submit-storage`, `generated-frame-plan` | Bounded storage and reuse contracts for frame scheduling/submission. These do not prove that every driver or present path is allocation-free. |

For allocator or resource-lifetime code changes, run the full Renderer tests and appropriate sanitizer coverage, then select MAKO Gym evidence in proportion to the boundary: `runtime-overhead` for private allocation and CPU/RSS observations, `sustained-health` for repeated-resource plateaus, `sync-validation` for completion/ownership changes, and the applicable feature, recovery, or compositor suites. Hardware qualification also includes the selected `constraints` cases under [Testing MAKO](../../TESTING.md). Exercise both FP32 and FP16 for affected AMD Frame Generation paths, and record untested architectures, drivers, runtimes, and rows explicitly. Documentation-only changes do not establish new hardware evidence.

| Responsibility | Source of truth |
| --- | --- |
| Image pool and cursor | [`image_memory_pool.hpp`](../mako-common/include/mako-common/vulkan/image_memory_pool.hpp), [`image_memory_pool.cpp`](../mako-common/src/vulkan/image_memory_pool.cpp) |
| Allocation, memory-type selection, and handle ownership | [`image.cpp`](../mako-common/src/vulkan/image.cpp), [`buffer.cpp`](../mako-common/src/vulkan/buffer.cpp), [`vulkan.cpp`](../mako-common/src/vulkan/vulkan.cpp), [`pointers.hpp`](../mako-common/include/mako-common/helpers/pointers.hpp) |
| Allocation counters | [`device_memory_accounting.hpp`](../mako-common/include/mako-common/vulkan/device_memory_accounting.hpp), [`device_memory_accounting.cpp`](../mako-common/src/vulkan/device_memory_accounting.cpp) |
| Swapchain resource groups and replacement | [`swapchain.hpp`](../mako-render/src/swapchain/swapchain.hpp), [`resources.cpp`](../mako-render/src/swapchain/resources.cpp), [`create.cpp`](../mako-render/src/swapchain/create.cpp) |
| Backend construction, pools, and deferred context close | [`mako.cpp`](../mako-backend/src/mako.cpp), [`utils.hpp`](../mako-backend/src/helpers/utils.hpp), [`shaderchains/`](../mako-backend/src/shaderchains/) |
| Scaling admission and resource formats | [`spatial_scaling_policy.hpp`](../mako-render/src/spatial_scaling_policy.hpp), [`spatial_scaler.cpp`](../mako-render/src/spatial_scaler.cpp) |

Extend these owners when changing memory behavior. Keep the allocator, accounting, failure cleanup, focused tests, and owning boundary guide aligned; do not add a second allocator or treat a policy estimate as completion or allocation proof.
