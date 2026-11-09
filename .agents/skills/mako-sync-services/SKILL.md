---
name: mako-sync-services
description: Commit and push coordinated work across the local MAKO, vkBasalt fork, MAKO FG, MAKO Gym, and MAKO Traces repositories. Use when the user explicitly asks to sync or commit and push all MAKO services together.
---

# Sync MAKO services

Commit and push the five coordinated repositories only after a complete read-only preflight. This workflow is not atomic across repositories, so eliminate avoidable failures before the first commit or push and report any partial completion precisely.

Execution boundary: Git inspection, commits, and pushes run on the host. This skill does not start Docker or Podman; a selected repository validation command may use a container according to that repository's own instructions.

Inspect and operate on the actual host (`exec_command` with `sandbox_permissions: "require_escalated"` in Codex): the default isolated shell may hide sibling checkouts, their build tools, or network access. Do not infer that a checkout or prerequisite is missing from a sandbox-only failure.

## Repository map

Resolve the MAKO root with `git rev-parse --show-toplevel`, then resolve these sibling checkouts from its parent directory:

| Repository | Checkout | Required local branch | Push destination |
| --- | --- | --- | --- |
| MAKO | MAKO root | Current non-detached branch | Its upstream on `mako-private` when that remote exists (for example `phoenix` → `mako-private/main`); otherwise the same branch on `origin` |
| vkBasalt fork | `../vkBasalt` | `mako-master` | `origin/master` |
| MAKO FG | `../MAKO-FG` | `main` | `origin/main` |
| MAKO Gym | `../MAKO-Gym` | `main` | `origin/main` |
| MAKO Traces | `../MAKO-Traces` | `main` | `origin/main` |

Verify that every checkout exists and that each `origin` identifies the expected `eugeniosegala/MAKO`, `eugeniosegala/vkBasalt`, `eugeniosegala/MAKO-FG`, `eugeniosegala/MAKO-Gym`, or `eugeniosegala/MAKO-Traces` GitHub repository. Accept normal HTTPS or SSH URL forms. Do not substitute another checkout or remote.

A MAKO checkout with a remote named `mako-private` keeps its working branches in that private repository, which must identify `eugeniosegala/MAKO-PRIVATE`. There the current branch tracks a branch on `mako-private` (usually its `main`), and that tracked branch is the MAKO destination. The public `origin` receives only `main`, only when the user explicitly asks to publish it, and the tracked `.githooks/pre-push` guard refuses any other branch or non-main commit for `origin`. Never push a working branch to `origin` in such a checkout, and never bypass the guard.

MAKO FG is private. Keep its architecture and training source in its own repository; follow its source-control and release rules for allowed records and release folders. Do not copy its research source, raw evidence, or workspace inputs into MAKO or another checkout during sync.

## Workflow

1. Announce that the five-repository sync is starting. Read the applicable `AGENTS.md` and repository-owned validation guidance in every checkout before acting.
2. Inspect branch, status, staged and unstaged diffs, untracked files, upstream state, and any in-progress Git operation in all five repositories. Do not switch branches. Stop before mutation if a required branch or remote is wrong, a repository is conflicted, or a merge, rebase, cherry-pick, or bisect is active.
3. Fetch each destination branch from its remote (`mako-private` for a MAKO checkout that has it, otherwise `origin`) and compare it with the local `HEAD`. Stop before committing if any destination is ahead or diverged. A clean repository with existing local commits ahead is valid and still needs to be pushed.
4. Review all pending files for credentials, licensed inputs, generated evidence, build output, archives, dumps, or other protected content according to that repository's instructions. Stop before staging if anything unsafe or unintended is present.
5. Run the portable checks required by each repository in proportion to its pending changes. Validate every repository before committing any of them. For MAKO FG, follow its `AGENTS.md` and `tests/README.md`; `bash scripts/check.sh` owns the complete portable gate. Do not start training, rent compute, export or select a model release, or deploy as part of sync. Do not bypass hooks or skip a failed check.
6. In each repository with pending changes, stage the complete worktree with `git add -A`, inspect the staged diff, and create one accurate repository-specific commit. Use a user-supplied commit message where it accurately describes that repository; otherwise derive a concise conventional message from its diff. Do not amend or rewrite existing commits.
7. After all local commits succeed, push in this order with ordinary non-force pushes: MAKO current branch to its destination (`git push mako-private HEAD:main` for the usual private setup), `vkBasalt` `HEAD:master`, MAKO FG `main`, MAKO Gym `main`, then MAKO Traces `main`. If a push fails, stop before later pushes and report exactly which repositories are already remote and which remain local.
8. Verify every worktree is clean and every local `HEAD` equals its intended remote destination. Return a compact table with repository, branch mapping, commit SHA, push result, and tests run.

Never pull, merge, rebase, reset, clean, stash, switch branches, amend, tag, publish a release, or force-push as part of this skill. A remote-ahead state or failed validation requires user direction rather than an automatic history change.
