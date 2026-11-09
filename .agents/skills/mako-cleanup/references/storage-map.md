# MAKO storage boundaries

Re-read current owning scripts on each run; environment overrides and layouts can change. Relative MAKO paths below start at the MAKO repository root.

## Discover the whole shared tree

Inventory every child of `~/mako`, including repositories, worktrees, hidden directories, loose archives, and non-Git output directories. Resolve the actual root and repository identities; the names below are routing examples, not an exhaustive allowlist. Read each owner’s instructions, ignore rules, and storage/build guides before classifying its files. Report excluded or inaccessible children explicitly. Do not follow a symlink into unrelated storage or count the same physical data twice.

## Keep reusable build inputs

| Storage | Reason to retain |
| --- | --- |
| `engine/build/cache/` | Flatpak data, `native-sdk`, `vulkan-headers`, `ccache`, `vkbasalt`, and `lsfg-fp16-tools`; preserve the whole cache |
| `MAKO_BUILD_CACHE_ROOT`, `MAKO_NATIVE_SDK_DIR`, `MAKO_FLATPAK_CACHE_ROOT` override paths | Relocated dependencies; resolve relative bases from the owning scripts |
| Docker/Podman `mako-flatpak-cache` volume, other verified build-cache volumes, and builder images | Portable Flatpak packaging mounts `/cache`; cache and work roots can both point there, so the name `work` does not make it disposable |
| `.flatpak-builder` directories, Flatpak installations, SDKs, runtimes, and downloads | Warm build environment, including `engine/.flatpak-builder` and `plugin/.flatpak-builder` |
| Host compilers, Qt, multilib, shader tools, Vulkan headers, container runtime, Node/pnpm/Python and shared environments | Regular build and release tooling; distinguish these from task-private experiment environments |
| Regular build/test `node_modules`, pnpm/npm stores, shared virtual environments, compiler/download caches, third-party sources and SDKs | Reusable dependencies; dependencies private to a completed disposable experiment are eligible with that environment |
| Actively used candidates and dependency overrides | `MAKO_VKBASALT_LOCAL_ARCHIVE`, for example, can point inside `engine/out`; inspect consumers before deletion |

Owners: `engine/docs/BUILDING-FROM-SOURCE.md`, `engine/scripts/package-local.sh`, `engine/scripts/package-flatpaks.sh`, `engine/scripts/build-steamos-dev.sh`, `plugin/docs/PACKAGING.md`, and `HOW_TO_RELEASE.md`. Inspect only relevant environment variables; do not dump the environment or secret-bearing config.

**Never use `engine/scripts/prune-build-cache.sh --confirm` or `engine/scripts/prune-steamos-flatpak-cache.sh --confirm` for this workflow. Both delete caches the user wants to keep.** Their default read-only reports do not implement this retention policy.

## Inspect for removal

| Area | Potentially disposable material | Required distinction |
| --- | --- | --- |
| Renderer | Incremental CMake trees, `engine/build/work`, binaries, packages and staging in `engine/out`, reviewed `engine/build-*` trees | Enumerate around caches; inspect nested dependencies and active candidate references |
| Decky | `plugin/out`, `plugin/dist`, `plugin/coverage`, generated reports, packaging staging and screenshots | Keep dependencies and originals not copied to their source owner |
| Website | `website/dist`, `dist-pages`, `.vinext`, `.next`, preview screenshots | Read `website/README.md`; preserve source, `public`, assets, and dependencies |
| vkBasalt | Meson/CMake output and completed package archives | Read its packaging scripts and ignore rules; keep downloaded source/toolchains |
| Other/experimental sibling repositories | Verified builds, packages, coverage, logs, temporary captures, abandoned task-private environments | Discover every checkout; retain source, Git history, regular build/test environments, and build caches |
| MAKO-FG | Completed/abandoned run payloads, superseded checkpoints and exports, previews, rebuildable data derivatives and teacher caches, duplicate transfers, native/test builds, and experiment-private environments | Follow the measured keep-set and historical-retirement procedure below; inspect mixed workspace trees by child and identify lost resume/reproduction capability |
| MAKO Traces | Generated validator/test scratch, bytecode, and verified unarchived staging outside curated archives | Preserve `traces/`, `user-reports/`, checksums, provenance, and source; read its archive rules |
| MAKO Gym | Completed/failed run logs, QA images and raw captures, repeated comparison outputs, obsolete extracted candidates, dated reports in `out`, generated fixtures, and one-off test environments | Retain current baselines and tools/workloads actually reused by regular QA, such as `out/renderer-comparison/tools`; inspect historical baselines and experiment-specific dependencies for retirement. Disclose lost measurements and protect active investigations |
| Codex | MAKO archived threads, unused images/charts/attachments, completed disposable worktrees | Follow the Codex procedure; establish ownership and lack of remaining consumers |
| Claude and Kimi | Archived chats/sessions, verified unreferenced task assets, logs, caches, and superseded installer copies | Follow `ai-apps-cleanup.md`; identify each app’s local/cloud ownership and deletion interface before selecting targets |

Prioritize generated test storage: CTest `Testing` output, sanitizer/test build trees, coverage and test reports, Gym comparison images, logs and captures, completed-run temporary fixtures, `__pycache__`, `.pytest_cache`, and verified MAKO-owned pytest temporary directories. Include completed, failed, and abandoned runs once they are no longer in use. Preserve tracked test cases and fixtures, curated baselines, and reusable test tools; distinguish those from disposable generated binaries and run data. Check tracked paths before proposing deletion. Ignore rules also protect licensed inputs, private evidence, and environments; ignored does not mean disposable.

## MAKO logs and recordings

Include MAKO-generated logs and recordings in every cleanup audit, including recent completed sessions. Read the current diagnostics guides, launchers, Decky logging configuration, and recording owners (`engine/mako-render/src/frame_recorder.cpp` and `plugin/py_modules/mako_plugin/frame_recording.py`) to resolve actual paths; these locations are discovery examples, not deletion patterns.

| Storage | Discovery and classification |
| --- | --- |
| Renderer diagnostics | Inspect `~/.config/mako-render/present-diagnostics.log` and numbered rotations, plus configured log destinations and attributable temporary session logs. Keep logs still being written or needed by an active investigation. |
| MAKO Decky logs | Inspect `~/homebrew/logs/Mako` or the configured Decky log root for MAKO's current and rotated logs. Preserve unrelated plugins' logs and the installed plugin, settings, and profiles. |
| Exported diagnostics | Inspect attributable reports and session logs on Desktop, Downloads, temporary roots, and verified custom destinations. Do not delete shared Steam console logs or system journals merely because they contain MAKO records. |
| MAKO frame recordings | Inspect `~/.local/share/mako-render/captures` and the Renderer's `XDG_DATA_HOME` equivalent, including complete session directories, `recording.json`, raw frame payloads, and interrupted recording scratch. Check actual producers and consumers before selecting exact session paths. |
| Recording copies and derived media | Follow verified recording/task references into MAKO Gym output, FG workspace captures, temporary exports, and videos or comparisons. Inspect each copy's purpose and retained references; a video is not automatically a replacement for raw source frames and timing metadata. |

Propose completed, unreferenced MAKO logs and recordings for removal; there is no age cutoff. Keep recordings required by active datasets, current model qualification, curated Traces archives, or ongoing investigations. For captures referenced only by historical experiments, use the FG retirement procedure below even when they live outside FG; distinguish replaceable derived media from unique source recordings. Unknown ownership or unresolved retention belongs in `review`, excluded from promised savings.

Before deleting any session, recheck active game/recorder processes, open writers, and capture-directory activity. Recording roots can contain live rolling buffers, `armed.*` markers, request/hold control files, and status metadata; do not call helpers that prune markers or write requests during discovery, truncate a live log, or remove a whole recording root with active or protected children. Inventory stale control/scratch files separately and remove only approved, inactive items. Report the loss of original frames, timestamps, and session diagnostics in the approval plan, and count each physical file only once across log, recording, and test subtotals.

## MAKO-FG retention and cross-repository dependencies

Treat MAKO-FG and MAKO Gym as primary reclamation targets, including their large mixed workspaces. Read their `AGENTS.md`, README, FG experiment/data/native guides, and Gym storage/suite owners. Use history records and read-only `history locate` where available to map payloads to runs, checkpoints, exports, datasets, baselines, and consumers. Measure actual local use; a reported workspace total is not automatically reclaimable space.

Build a measured keep set from current consumers: installed/selected models and exports, active training and required resume state, chosen controls/baselines used by ongoing comparisons, active dataset inputs, and essential shared environments/toolchains. Verify scripts, manifests, history status, processes, and cross-repository references; neither inactivity nor a failed text search proves disposability. A historical record is evidence of what would be lost, not by itself a requirement to keep every payload forever.

| Large allocation | Reclamation decision |
| --- | --- |
| Training runs and checkpoints | Separate current/resumable work and selected controls from intermediate, superseded, failed, or abandoned trials. Propose unused intermediates for removal when no retained run needs them; list unique historical checkpoints as named retirement options with the loss of model weights and resume/exact-reproduction capability. Do not choose by model score or age alone. |
| Exports, evaluation output, previews, and native builds | Keep exports installed or consumed by current Renderer/Gym configurations. Propose superseded copies, completed evaluations, generated binaries, derived videos, and rebuildable previews; account separately for retained source models and conversion inputs. |
| Datasets, captures, and teacher caches | Separate originals/manifests/licensed inputs from duplicate transfers, extracted copies, generated crops/frames, temporary shards, and teacher-derived caches. Propose unused derivatives when regeneration inputs remain and state compute/download cost. Unique source captures and non-reproducible caches require explicit historical retirement; do not describe them as freely regenerable. |
| Gym runs and comparison evidence | Propose completed images, logs, profiler traces, repeated captures, extracted candidates, and generated fixtures no longer needed by active checks. Keep selected current baselines and reproducibility inputs; offer obsolete historical baseline groups for retirement with their dependent run IDs. |
| Environments and staging | Keep one shared setup per genuinely required runtime/toolchain, not every experiment's copied dependencies. Propose completed task-private virtual environments, sandboxes, prefixes, containers, and unused transfer/staging directories with their private dependencies. Distinct architectures or incompatible toolchains can justify multiple retained setups. |

Distinguish ordinary regenerable cleanup from retirement of unique historical data. FG's owner rules may deliberately retain stopped and weaker trials or every reproduction bundle. In that case, identify exact historical groups and sizes, quote the relevant retention requirement in the report, and explain the lost comparisons or reproduction capability. Offer them for an explicit user retention decision instead of marking the whole workspace untouchable. Approval must cover that named retirement and retention exception; a generic cleanup instruction alone does not silently override it. Once approved, do not ask again solely because the same historical references still exist.

Keep compact metrics, configuration, lineage, manifests, and checksums where useful and consistent with the owner's format. Use an existing supported retirement/missing-artifact mechanism if one exists; otherwise record the removed paths and affected history IDs in the cleanup report. Do not invent a history command, rewrite scientific results, erase failure records, or claim a reproduction remains available after its required payload was removed. Avoid making new full-size backups of approved disposable data; preserving a small audit record should not duplicate the storage being reclaimed. Source, licensed inputs, unrelated user datasets, curated Traces archives, and active work remain protected.

Follow consumers across repositories: Gym trials may supply current FG baselines, and temporary scripts/files may belong to active FG tasks. Apply the keep/retire decision to every verified copy and consumer, including paths outside these checkouts. A name containing `fg` alone does not establish ownership. Remote worker disks are separate from local savings; discovery does not authorize stopping workers or deleting remote storage.

## Outside-checkout storage

Inspect these areas with bounded listings and size summaries, then drill into attributable candidates. Respect `TMPDIR`, XDG overrides, configured build roots, and actual container-manager storage roots. Do not dump thousands of filenames, transcript bodies, credentials, or whole process environments into the report.

| Area | Discovery and classification |
| --- | --- |
| `/tmp`, `/var/tmp`, configured temporary roots | MAKO task scripts, logs, images, compiler/test scratch, extracted candidates, virtual environments, and Wine/Proton test prefixes. Preserve unique authored scripts, active task files, runtime sockets and shared service directories; filenames alone do not prove completion or ownership. |
| Downloads and other verified export destinations | Old tester/release ZIPs, host/Flatpak bundles, extracted installers, diagnostics, and captures. Check provenance and active consumers; preserve unique user captures, inputs, source and currently needed candidates. |
| `~/.local/share/Trash` or the applicable filesystem Trash | Use `.trashinfo` original paths and deletion metadata to attribute MAKO items. Pair each exact payload with its metadata in the plan. Permanent removal eliminates restore capability; never empty unrelated Trash globally. Check large top-level files as well as directories. |
| XDG cache roots, normally `~/.cache` | MAKO UI/test caches and task-specific scratch are candidates. Preserve shared compiler, Python/Node package, shader, Flatpak, model/data, and Codex runtime caches unless explicitly in scope and verified disposable. |
| XDG data/state roots, normally `~/.local/share` and `~/.local/state` | Distinguish disposable test data from installed Renderer/Decky resources, profiles, model installs, application state, Steam/game data, Flatpak installations and managed container storage. Inspect installed paths for attribution, not as implicit uninstall targets. |
| `~/.local/bin`, `~/.local/lib`, `~/.local/lib32`, Decky installation roots | Preserve installed launchers, libraries, models, shared environments and tools. Only verified orphaned task-private output belongs in a cleanup plan; uninstalling or changing an active installation is a separate task. |
| Docker/Podman and other verified environment managers | Inventory exact images, containers, volumes and mounts through the owner. Distinguish superseded unused images from current shared builders and warm cache volumes; zero attached containers does not make a cache obsolete. |
| Codex home, task output directories and managed worktrees | Follow `codex-cleanup.md`; attribute by repository/thread metadata and preserve active or unrelated tasks and application runtimes. Archived chat size is not automatically reclaimable space. |

Record external paths and their owning repository/task alongside the repository inventory. Explicitly report areas not inspected or blocked by permissions. Keep shared application totals, protected caches, remote storage and already-counted bind mounts out of the proposed local savings.

Include [desktop cleanup](desktop-cleanup.md) in the normal audit for VS Code/browser caches, Steam/application logs, system journals, crash dumps, installers, and external test folders. Report shared desktop/system bytes separately from MAKO-attributed storage.

## Temporary experiment environments

The user explicitly wants environments Codex created for completed Gym tests, release checks, and experiments cleaned up, even if recent. Classify by purpose and current consumers: keep regular build/release/test environments, but include abandoned task-private environments and their installed dependencies in the proposed removal set. A Python virtual environment, container, or sandbox is not protected merely because it contains tools.

Start in the discovered repositories, their generated output and worktrees, and task-owned output directories. Follow concrete script, task metadata, or environment-manager references into `/tmp`, `/var/tmp`, `TMPDIR`, Codex temporary directories, or other user-owned locations. Use bounded discovery for `.venv`, `venv`, `pyvenv.cfg`, Conda environment metadata, isolated dependency installs, Wine/Proton test prefixes, extracted test roots, and similar disposable sandboxes. These are discovery clues, not deletion patterns. Do not scan or remove unrelated home-directory environments solely because their names look temporary.

Attribute each candidate to a MAKO task using its path, creation commands/task record, environment metadata, or container labels/mounts. Check current build and test scripts, launchers, scheduled jobs, and processes for consumers. Inspect a candidate's own metadata with system tools rather than activating it or executing its Python/startup scripts. A copied interpreter's link to a shared installation does not authorize deleting the interpreter target. Do not follow symlinks out of the selected environment. Preserve user-authored experiment scripts, unique source changes, and licensed inputs; split these from generated environment directories when they coexist.

Read-only container inventory should identify stopped MAKO experiment containers, task-specific images, volumes, and writable layers. Preserve the portable release builders, Flatpak cache volumes, shared base images, regular QA runtimes, and active containers. Use runtime-reported exclusive/reclaimable sizes where available; shared layers are not additive. Containers started with automatic removal may leave no resource to clean. Remove only approved exact IDs through the owning runtime; never delete its storage directories directly or perform a global prune. A task-private Wine/Proton prefix can be disposable; installed Steam runtimes, Proton versions, game prefixes, and user game data remain protected.

Codex's bundled runtime, installed packages/plugins, and environments powering the current assistant or supported tools are not experiment leftovers. If an experiment used a remote or managed sandbox, report its host and storage separately and use its supported lifecycle interface only when available and authorized. Do not claim such a sandbox exists or contributes reclaimable local bytes without evidence.

## Preserve source and durable data

Keep `.git` stores, branches, worktrees with unique/uncommitted work, tracked assets and release artwork, documentation, config, credentials, licensed inputs such as `Lossless.dll`, installed Renderer/Decky, profiles, and game data. MAKO Traces' `traces/` and `user-reports/` are curated integrity-protected archives. Inspect Git size if useful, but do not rewrite history, expire reflogs, or prune Git objects as ordinary cleanup.

Unused source assets are a source-maintenance decision. This cleanup covers temporary generated assets with verified provenance and no remaining references. Old timestamps or a failed text search alone do not prove an asset is unused.
