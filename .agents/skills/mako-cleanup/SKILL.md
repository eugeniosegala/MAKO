---
name: mako-cleanup
description: Reclaim as much unnecessary disk space as practical across ~/mako and desktop/system leftovers. Audit MAKO-FG and Gym experiments, builds, recordings, logs, caches, temporary environments, containers, and Codex/Claude/Kimi history and installers. Rank measured targets by savings, explain historical artifacts that can be retired, and confirm the exact deletion plan. Preserve current work, source, Flatpak caches, and essential shared tooling.
---

# MAKO cleanup

The goal is to reclaim as much unnecessary disk space as practical while keeping a minimal working development setup. Prioritize the largest allocations across the whole MAKO tree, especially MAKO-FG and MAKO Gym experiments, plus desktop caches, system logs, crash dumps, temporary files, and AI-app leftovers. Do not stop after deleting small logs when large completed experiments remain unexplored. **There is no age cutoff; recent completed output is eligible too. Show the concrete cleanup report and obtain the user's explicit confirmation of the deletion set before deleting anything.** A report-only request ends after the report.

Cleaning test leftovers is a primary purpose of this skill. Explicitly inspect completed, failed, and abandoned test runs, generated test binaries and sanitizer builds, coverage, logs, screenshots, rendered comparisons, reports, temporary fixtures, and disposable test environments. Include recent results too. Preserve test source, required fixtures and current baselines, essential shared tooling, protected build caches, and files still in use. Do not retain every completed result, historical baseline, model checkpoint, or experiment environment merely because it might be useful again; apply the experiment-retirement procedure below.

## Discover and classify

The normal discovery scope includes the whole shared MAKO tree, attributable external storage, and the desktop/system categories below. MAKO-FG and MAKO Gym are included by default. Honor exclusions that apply to the current run, including their cross-repository inputs and evidence; a one-run exclusion does not become a permanent skill exclusion. Updating the skill itself does not delete data.

1. Locate the shared MAKO directory: normally `/home/deck/mako`, otherwise `~/mako` or the parent of the current MAKO checkout. Enumerate every immediate child, including hidden and non-Git directories, then discover repositories and worktrees within relevant children. Expect MAKO, MAKO-FG, MAKO-Gym, MAKO-Traces, vkBasalt, and historical or experimental checkouts, but discover new siblings rather than limiting inspection to these names. Record symlink targets and separate filesystems without automatically traversing them. Inspect each repository's branch, worktree, `AGENTS.md`, `.gitignore`, and owning build/storage guides. Preserve source and local changes. Do not switch branches, commit, push, or delete whole repositories.
2. Read [the storage map](references/storage-map.md), including its MAKO-FG, Traces, and outside-checkout sections. Trace current build scripts and path overrides before classifying storage. Build directories can contain reusable SDKs, Flatpak data, and dependency downloads. Do not treat `build`, `out`, `assets`, `cache`, or Git-ignored status alone as proof of disposability.
3. Measure disk use by filesystem and rank the largest directories before investigating small items. Drill into each large MAKO-FG/Gym workspace, run, dataset/cache, checkpoint/export, and test-output tree using [the experiment procedure](references/storage-map.md#mako-fg-retention-and-cross-repository-dependencies). Account for the substantial allocations as proposed removals, required retained data, or unresolved retirement options; do not mark a whole workspace protected because it mixes these categories. Use read-only `du -x`/`df`, Git inspection, and container inventory. Use host execution permissions when needed to inspect the actual container store; report inaccessible storage as unmeasured. Do not install tools, build projects, download packages, or clean anything during discovery.
4. Inspect Codex storage using [the Codex procedure](references/codex-cleanup.md). Establish MAKO ownership from project paths, repository identity, and thread IDs. Protect the current thread, running or otherwise in-use tasks, and unrelated chats. Reconcile archived IDs, rollout paths, and descendant relationships with the read-only state database, and discover the installed permanent-delete CLI/API before treating deletion as unavailable. Inspect metadata first; do not dump transcripts or credentials into the report.
5. Inspect [temporary experiment environments](references/storage-map.md#temporary-experiment-environments), including Python virtual environments, one-off Gym/release sandboxes, temporary prefixes, and stopped experiment containers. Follow verified MAKO/Codex task references outside the repositories into temporary or task-owned directories. Environments made only for completed experiments are eligible for removal, including their private dependencies; being a virtual environment does not automatically make one protected build tooling.
6. Explicitly inventory [MAKO logs and recordings](references/storage-map.md#mako-logs-and-recordings), including diagnostics rotations, Decky logs, exported reports, real-frame captures, and derived videos. Include recent completed sessions; protect active writers and retained FG/Traces inputs. Inspect [outside-checkout storage](references/storage-map.md#outside-checkout-storage): temporary roots, Downloads, Trash, XDG cache/data/state roots, and installed or relocated MAKO paths. Attribute candidates before selecting them; shared application totals are context, not reclaimable MAKO bytes.
7. Include [desktop cleanup](references/desktop-cleanup.md) in the normal audit: VS Code/browser caches, application and system logs, archived journals, crash dumps, old installers and rollback copies, and external test folders. Separate these from MAKO-attributed bytes and present the exact cache paths or bounded owner maintenance operation for approval. Inspect shared application data without treating entire profiles, installed apps, or system directories as disposable.
8. Include Claude and Kimi archived-chat/session storage, associated artifacts, logs/caches, and old installers in discovery using [Claude and Kimi cleanup](references/ai-apps-cleanup.md). Report unrelated-project/account history separately from MAKO-owned data; include exact conversations and any cloud deletion consequences in approval before removing them.
9. Build an exact-path plan: `remove` for verified disposable material or explicitly proposed historical retirement, `keep` for currently required/protected material, and `review` for unresolved ownership, retention decisions, or deletion mechanisms. Distinguish regenerable cleanup from retirement that loses unique experiment history. Resolve uncertainty independently where possible. Include eligible archived chats, generated assets, and disposable environments, not just repository builds.

Propose removing all verified disposable output, including recent completed MAKO Gym runs and post-release candidates. Establish a small, justified keep set based on current builds, installed models, active training/resume needs, selected comparison baselines, and live investigations. Explain the consumer for each substantial retained allocation; historical registration or a possible future use alone should lead to a retirement option, not automatic permanent retention. Explicitly disclose lost measurements, checkpoints, resume capability, or exact reproducibility before asking to retire history. Preserve source, licensed inputs, unique user data, curated MAKO Traces archives, Flatpak caches, and essential shared build/release tooling. Removing incremental build trees causes recompilation; regenerating experiment data may require costly compute or unavailable inputs.

## Measure and report

Write a temporary JSON plan outside every deletion target. Each entry has an absolute `path`, `decision` (`remove`, `keep`, or `review`), and `reason`. Optional labels such as `repository` and `thread_id` may be included. Split mixed trees into explicit children: list `engine/build/cache` as `keep` and verified disposable siblings separately. Never propose deleting a parent with a protected child.

For managed environments, container resources, or journal maintenance, also record the owning manager/runtime, host, exact resource ID, environment prefix, or journal scope, and its targeted removal operation. For journal vacuuming, include the proposed retention bound and the archived history it can remove rather than presenting the whole live journal directory as an exact-path deletion target. Measure these through the owner's read-only inventory and account for shared layers separately; the filesystem helper only measures literal paths. Do not count a container's bind-mounted directory both as repository data and container storage, or remote disk savings as local free space.

```json
{
    "entries": [
        {
            "path": "/absolute/MAKO/engine/build/cache",
            "decision": "keep",
            "reason": "Reusable build dependencies"
        },
        {
            "path": "/absolute/MAKO/plugin/out/tester.zip",
            "decision": "remove",
            "reason": "Completed release's tester package"
        }
    ]
}
```

Run the bundled read-only helper with resolved paths:

```bash
python3 /path/to/mako-cleanup/scripts/measure.py /tmp/mako-cleanup-plan.json > /tmp/mako-cleanup-measurement.json
```

The helper has no deletion mode. It rejects overlapping roots and symlinked path ancestors, does not follow symlinks or descend into mounts, counts allocated blocks once per inode, and excludes hard-linked data from reclaimable bytes unless all links are selected for removal. It reports errors and metadata fingerprints for rechecking. It measures a plan; it does not decide whether deletion is safe. Estimates do not account for snapshots, reflink sharing, compression, or files held open by processes. Do not include incomplete rows or `review` entries in promised savings.

Present a concise report containing:

- A coverage ledger for every discovered child of the shared MAKO directory and each relevant external storage area: inspected, excluded by the user, protected, unavailable, or still needing review. Do not describe an uninspected area as clean.
- A largest-first table of estimated reclaimable GiB, per-repository/category and external-location subtotals, and exact paths or a linked complete manifest. Show separate totals for regenerable cleanup and proposed historical retirement; exclude unresolved opportunities. Identify storage that was not measured.
- MAKO-FG and MAKO Gym summaries showing total inspected storage, proposed removals, the measured keep set and its current consumers, and unresolved large allocations. Cover checkpoints, exports, run evidence, data derivatives/caches, builds, and environments. Give a test-cleanup subtotal across repositories without counting shared items twice.
- A MAKO logs-and-recordings subtotal with resolved directories, completed-session log and recording sizes, and retained or active captures. Explain that deleting original recordings loses their real frames and metadata; distinguish derived videos from source captures and avoid counting the same files again under test output.
- Separate Codex, Claude, and Kimi subtotals with exact chat/session titles and IDs, the owning account/host, and deletion scope, including descendants or linked sessions where applicable. Separate local reclaimable bytes from cloud history deletion; state permanent deletion consequences.
- A disposable-environment subtotal with exact paths or resource IDs, the completed MAKO task that created each, and why it is no longer used by regular builds or tests. Identify shared environments retained for future work.
- Desktop/system subtotals for editor/browser caches, application/system logs and journals, crash dumps, installers, and temporary files. Separate disk-backed bytes from tmpfs and identify any administrator-authentication requirement.
- What is kept and why, with measured sizes for the largest items: protected Flatpak caches/SDKs, essential shared toolchains/dependencies, active work, source, installed MAKO, and selected durable evidence.
- The largest remaining allocations and unresolved opportunities, excluded from the proposed total; explain their consumer or retention policy, what removal would lose, and what decision or evidence is missing. Do not declare cleanup exhausted while large unclassified experiment trees remain.
- Expected rebuild cost, estimation limits, and free space on affected filesystems before cleanup.

Ask whether to proceed with that exact removal set. Explain that the pause implements the user's explicit report-before-deletion requirement and link this `SKILL.md`. Silence, a timeout, or the initial invocation is not confirmation. Keep the report and plan for the continuation; never run deletion in the same tool call as the question.

## Clean the approved set

1. Match confirmation to the reported paths and thread IDs. Excluding items needs no new approval. Adding items or changing a deletion's consequences requires an updated report and confirmation for that change.
2. Recheck Git state, process/container activity, thread status, path identities, nested repositories, mounts, protected descendants, and references. Re-run measurement and compare fingerprints for proposed removals. Skip changed or newly active items and report them; do not expand the plan or kill builds to free space. Check proposed directories for untracked source, licensed inputs, SDKs, or dependencies missed during classification. Recheck cross-repository consumers, especially FG history/export/input references and retained Traces evidence. For approved experiment retirement, verify the exact historical records and acknowledged retention exceptions; inactivity alone is insufficient, and an old history link is not a new blocker when its loss was already explicitly approved.
3. Delete only approved, revalidated paths with literal arguments or argument arrays, without following symlinks or crossing mounts. For managed environments and stopped experiment containers, use the owner's exact environment prefix/resource ID and verify attached volumes separately. Use an owning cleanup command only if its exact scope matches the plan. Do not use broad globs, `git clean -fdx`, resets, global Docker/Podman pruning, global environment/package-cache purges, Flatpak pruning, or MAKO cache-prune scripts' `--confirm` modes. Never delete all of `engine/build`. Ordinary execution permissions still apply.
4. Delete approved Codex threads through a supported interface, following the Codex procedure. Verify cascades stay inside the approved set. Remove unused associated files only when separately inventoried and approved. Archiving a chat is not deletion, and worktree archival may retain a recovery snapshot; count only measured reclaimed bytes.
5. Re-measure affected filesystems, verify protected caches/tooling and Git source state remain intact, and report deletions, skipped/failed items, observed free-space change, and remaining opportunities. Concurrent writes can distort `df`; distinguish observed change from sizes removed. Do not rebuild deleted output to test cleanup. Keep the small report and manifest, not gigabytes of backup copies.

Finish with the exact branch and staged/committed/pushed state of repositories involved. This skill does not deploy or change installed MAKO components.
