# Codex chats and assets

Resolve storage from `CODEX_HOME`, falling back to `~/.codex`. Inspect only MAKO-owned data. Preserve config, credentials, skills, plugins, runtime packages, automations, active sessions, shared databases, and unrelated projects. Never bulk-delete Codex home or shared asset directories.

## Attribute and inventory

Prefer app tools for projects, threads, archived threads, and artifacts; paginate archived results fully. Match canonical project roots under the shared MAKO directory or a worktree's verified Git common directory/remote, not the word “MAKO” in a title. A projectless chat needs corroborating metadata or user identification. Use exact source-provided titles in the report.

Reconcile the app inventory with the Codex state database and rollout paths. Do this during discovery, not only after a deletion failure. A JSONL file’s first `session_meta` record can corroborate `cwd` and thread ID, but a filename alone does not prove ownership, archive status, or inactivity.

### Database-backed discovery

1. Resolve the actual Codex home and host used by the desktop and CLI. Inspect `codex --version` and `codex delete --help` before concluding that permanent deletion is unavailable. Do not substitute another profile, home or host just to make a command succeed.
2. Discover candidate SQLite files under that home and any configured state location. Inspect `sqlite_master` and `PRAGMA table_info` through a URI connection with `mode=ro` and `PRAGMA query_only=ON`. Keep normal WAL visibility; do not use `immutable=1` against a live store or copy only the main database while ignoring its WAL. Do not print full rows or secret-bearing configuration.
3. Identify the authoritative thread state by schema, not filename/version. This installation was verified with CLI 0.147.0 and `state_5.sqlite`: `threads` contains `id`, `cwd`, `title`, `name`, `rollout_path`, `archived`, `is_pinned` and `project_id`; `thread_spawn_edges` contains `parent_thread_id` and `child_thread_id`. These are observed examples, not permanent schema assumptions. The desktop’s `sqlite/codex-dev.db` catalog and summaries database are separate stores; their existence does not prove archive state or that deleting a catalog row deletes a conversation.
4. Select only identity, ownership, archive/pin status, rollout and relationship fields. Once those columns are confirmed, use a parameterized query such as `SELECT id, cwd, title, name, rollout_path, archived, is_pinned, project_id FROM threads WHERE archived = ?` with `(1,)`. Match canonical path boundaries and verified Git identity against the discovered MAKO repositories; do not use a title match or an unbounded path substring as authorization.
5. Read spawn edges and compute the complete transitive descendant set, including non-archived descendants, with cycle detection. Reconcile those IDs against active/pinned app threads and user exclusions. Include every affected descendant in the plan, or keep the ancestor if any descendant is protected or unresolved. A database row cannot establish that a thread is currently idle; use the active app/server too.
6. Record the exact host, Codex home, database path, root/descendant IDs, displayed titles, rollout paths, associated external files, and keep/remove decisions in the cleanup manifest. Missing or contradictory rows/paths require reconciliation, not guessed deletion. If the schema is unfamiliar, use supported `thread/list`/`thread/read` and the app inventory and report the database limitation.

Use short read transactions and close connections before mutation. Never manually delete/update thread, catalog, attachment, history or summary rows, and never remove shared database files to clean a chat.

Inventory `generated_images`, `visualizations`, and `attachments` by owning thread ID; include linked projectless output directories only after verifying ownership. Preserve the current thread and its assets. Archived status does not mean an asset is unused: check retained chats, attached artifacts, source/release artwork references, and ongoing work. If only an original exists, classify it for review rather than calling it a duplicate. Broad directory totals are not automatically reclaimable savings.

Include task-private virtual environments and experiment sandboxes referenced by completed MAKO threads, even when located outside the repository or Codex home. Follow [temporary experiment environments](storage-map.md#temporary-experiment-environments) to distinguish disposable installations from Codex's own bundled runtime and regular MAKO build/test tooling. Collect their paths and ownership evidence before deleting the chats that identify them.

For managed worktrees, inspect attachments, status, untracked/ignored inputs, unique commits, and active consumers. Use supported lifecycle tools only for worktrees they can manage. Account for retained recovery snapshots. Do not bypass ownership restrictions by removing checkout directories directly. Archiving the current chat or deleting the current checkout is outside this skill.

## Delete after approval

The official [Codex app-server documentation](https://learn.chatgpt.com/docs/app-server#delete-a-thread) documents `thread/delete` with `params: {"threadId": "..."}`. It removes persisted rollouts and metadata **and spawned descendants**. Include the full descendant set in the report and approval. Protect running, current, pinned, unrelated, or still-needed descendants; do not delete their ancestor if any would be affected. A loaded-thread query on a new connection does not prove inactivity on the desktop's existing server.

Choose a supported deletion route before reporting chat cleanup as blocked:

1. Use an available dedicated permanent-delete tool or app UI when it exposes the exact approved thread and descendants.
2. Otherwise, when `codex delete --help` confirms support, use the [documented CLI deletion command](https://learn.chatgpt.com/docs/cli/reference#codex-delete) against the same host/home. After approval of that exact ID and cascade, run `codex delete <APPROVED_THREAD_UUID> --force`. `--force` skips the command’s duplicate prompt; it does not authorize additional targets. Use UUIDs, never ambiguous titles, and do not add sandbox-bypass flags. For a remote host, use only its verified supported endpoint and normal authentication mechanism without exposing credentials.
3. If that CLI is unavailable, use `thread/delete` through an existing supported app-server connection. Do not launch an ad hoc second writer/server against the live store merely to bypass connection restrictions.

Delete one approved ancestor at a time and record its outcome before proceeding; descendants already removed by that cascade need no separate request. On a timeout or ambiguous response, first re-query the exact IDs and recorded rollout paths. If they are already absent, verify and record success rather than blindly repeating the mutation. On a permission failure, use the ordinary host execution approval path for the same operation. On wrong-home, wrong-host, missing-command, busy-thread or connection errors, resolve that specific cause and recheck activity; do not switch to SQL deletes. If no supported route works, report the exact failed route and reason and continue independent cleanup. Do not stop at “no delete tool” when the installed CLI provides one.

`thread/archive` and `set_thread_archived` retain conversation data. They do not implement permanent deletion or free the rollout's space. Official reference: [archive semantics](https://learn.chatgpt.com/docs/app-server#archive-a-thread).

## Verify records and clean residual files

After supported deletion, reopen a read-only database connection and verify every approved root/descendant ID is absent from authoritative thread state and its recorded rollout is gone. Refresh the app’s archived listing as well. Report a stale desktop catalog separately from surviving authoritative state; do not repair it by editing SQLite. Record partial results per ID and never claim that an archive operation permanently deleted a chat.

Re-inventory the manifest’s thread-owned assets, temporary files, attachments, generated images, visualizations, task-private environments and worktree resources. Remove only separately approved exact paths whose ownership and lack of retained consumers have been verified. Trace files outside Codex home before deleting their identifying chat metadata; a missing thread alone does not prove that a file is orphaned. Unknown or shared assets stay in `review`. Use the owning manager for worktrees/environments and preserve excluded MAKO-FG work wherever it lives.

Report chat/rollout deletion, residual-file deletion and unresolved items separately. Do not assume attachments, images, visualizations, snapshots, or SQLite files also shrank. Never vacuum/remove live shared databases, WAL files, locks, or global logs to pursue extra savings. Measure filesystem changes and report partial outcomes accurately.
