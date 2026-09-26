---
name: mako-sync-services
description: Commit and push coordinated work across the local MAKO, vkBasalt fork, MAKO Gym, and MAKO Traces repositories. Use when the user explicitly asks to sync or commit and push all MAKO services together.
---

# Sync MAKO services

Commit and push the four coordinated repositories only after a complete read-only preflight. This workflow is not atomic across repositories, so eliminate avoidable failures before the first commit or push and report any partial completion precisely.

## Repository map

Resolve the MAKO root with `git rev-parse --show-toplevel`, then resolve these sibling checkouts from its parent directory:

| Repository | Checkout | Required local branch | Push destination |
| --- | --- | --- | --- |
| MAKO | MAKO root | Current non-detached branch | Same branch on `origin` |
| vkBasalt fork | `../vkBasalt` | `mako-master` | `origin/master` |
| MAKO Gym | `../MAKO-Gym` | `main` | `origin/main` |
| MAKO Traces | `../MAKO-Traces` | `main` | `origin/main` |

Verify that every checkout exists and that each `origin` identifies the expected `eugeniosegala/MAKO`, `eugeniosegala/vkBasalt`, `eugeniosegala/MAKO-Gym`, or `eugeniosegala/MAKO-Traces` GitHub repository. Accept normal HTTPS or SSH URL forms. Do not substitute another checkout or remote.

## Workflow

1. Announce that the four-repository sync is starting. Read the applicable `AGENTS.md` and repository-owned validation guidance in every checkout before acting.
2. Inspect branch, status, staged and unstaged diffs, untracked files, upstream state, and any in-progress Git operation in all four repositories. Do not switch branches. Stop before mutation if a required branch or remote is wrong, a repository is conflicted, or a merge, rebase, cherry-pick, or bisect is active.
3. Fetch each destination branch from `origin` and compare it with the local `HEAD`. Stop before committing if any destination is ahead or diverged. A clean repository with existing local commits ahead is valid and still needs to be pushed.
4. Review all pending files for credentials, licensed inputs, generated evidence, build output, archives, dumps, or other protected content according to that repository's instructions. Stop before staging if anything unsafe or unintended is present.
5. Run the portable checks required by each repository in proportion to its pending changes. Validate every repository before committing any of them. Do not bypass hooks or skip a failed check.
6. In each repository with pending changes, stage the complete worktree with `git add -A`, inspect the staged diff, and create one accurate repository-specific commit. Use a user-supplied commit message where it accurately describes that repository; otherwise derive a concise conventional message from its diff. Do not amend or rewrite existing commits.
7. After all local commits succeed, push in this order with ordinary non-force pushes: MAKO current branch, `vkBasalt` `HEAD:master`, MAKO Gym `main`, then MAKO Traces `main`. If a push fails, stop before later pushes and report exactly which repositories are already remote and which remain local.
8. Verify every worktree is clean and every local `HEAD` equals its intended remote destination. Return a compact table with repository, branch mapping, commit SHA, push result, and tests run.

Never pull, merge, rebase, reset, clean, stash, switch branches, amend, tag, publish a release, or force-push as part of this skill. A remote-ahead state or failed validation requires user direction rather than an automatic history change.
