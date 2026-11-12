# Claude and Kimi chats, artifacts, and installers

Include these applications in the cleanup inventory when installed. Discover archived conversations, local coding/agent sessions, task-owned output, temporary environments, logs, caches, old installer downloads and rollback copies. Report personal or unrelated-project history separately; inclusion in discovery is not permission to delete every archived chat. Honor user exclusions and keep active/pinned/retained conversations and their dependencies.

## Identify the application and storage owner

Resolve desktop launcher paths, running executables, package ownership and the actual user-data/config paths. Distinguish desktop web conversations, local coding/agent sessions and CLI sessions even when they share branding. A missing CLI on PATH does not establish that the desktop app lacks deletion support. Check the installed app’s supported tools/UI, documented CLI help or local implementation as applicable; do not invent commands or copy Codex’s database schema or `thread/delete` protocol into another app.

Observed locations on this Linux host are discovery hints, not permanent interfaces:

| App | Locations | Important distinction |
| --- | --- | --- |
| Claude Desktop | `~/.config/Claude`, including `claude-code-sessions`, `local-agent-mode-sessions`, and browser storage; `~/.cache/claude-desktop` | Agent/code sessions and browser-backed conversations may have different owners. IndexedDB and Local Storage are application state, not disposable logs. |
| Claude installed app | `~/Applications/Claude.AppImage`, desktop launchers, actual download/build directories | Preserve the current launch target. Discover older installers/backup copies rather than assuming every AppImage is obsolete. |
| Claude CLI, if installed | Discover its configured home/session paths from current help or implementation; inspect `~/.claude` only if present | Do not assume CLI sessions and Desktop chat history are interchangeable. |
| Kimi Code app | `~/.config/kimi-code-app` | Electron caches, session storage and login state have different retention rules. Preserve profiles and credentials. |
| Kimi session service | `~/.kimi-code/sessions`, `sessions/.index-cache`, `cache/query-store`, `server`, `logs`, `credentials` | Session indexes and query stores may reference real history. Do not classify them as disposable solely because their parent is named `cache`. Protect credentials and active service/instance state. |

Inspect metadata first, not full transcripts or credential files. For SQLite-backed state, inspect the current schema with a read-only connection and normal WAL visibility, selecting only session identity, project, archived/deleted status, parent/link relationships and file paths. For other storage formats, use the app’s supported metadata interface. A session directory’s age or absence from the current sidebar does not prove it is archived or orphaned.

## Prepare a reviewable deletion set

Inventory each candidate with app/product, host, account identifier where available without exposing secrets, exact title/ID, archived status, project ownership, last activity, local file paths, related sessions/assets and proposed deletion route. Match all discovered MAKO repositories and verified worktree identities. Include other projects only within the user-approved broader set. Protect active sessions, unique source changes, saved outputs, shared attachments and files referenced by retained chats or repositories.

Determine whether the action deletes local history only or a synced/cloud conversation across devices. State that consequence before approval. Do not promise local disk savings for cloud-only deletion. If the app has no archive concept, label inactive sessions accurately instead of treating them as archived. Follow linked/child session relationships where the app’s deletion action cascades; exclude any ancestor with protected descendants.

Use the desktop-cleanup reference for closed-app cache handling and old-installer checks. Old installations, backups, packaging output and download archives must be exact separate targets; preserve the installed app and any requested fallback. Chat deletion does not authorize removing the whole app profile or uninstalling the app.

## Delete through the owner, then reconcile leftovers

After approval, prefer the app’s supported permanent-delete tool, API, CLI or UI for the exact conversation/session. Check available routes before declaring the task blocked; a missing connector does not rule out an installed CLI or desktop UI. Archiving/hiding is not deletion. If no supported route is accessible, report the precise missing capability and continue approved installer/cache cleanup; never improvise SQL mutations, remove live database/LevelDB files, or erase transcript directories behind the app’s back.

On timeout or an ambiguous result, re-query the exact ID before retrying. Verify local and, when applicable, synced history through the same owning interface. Distinguish a stale sidebar/index from a surviving conversation and use supported refresh/reconciliation. Do not rebuild indexes by hand, vacuum live databases, or delete shared WAL/SHM/lock files to force a result.

Recheck associated downloads, attachments, generated images, temporary files, exports and task-private environments after session deletion. Delete only approved exact paths with verified ownership and no retained consumers. Preserve originals, credentials, settings, plugins, active models/tools and unrelated history. An orphan candidate without sufficient provenance stays in `review`; “delete all temporary files” is not a global filesystem pattern.

Report per-app successes, remaining records/files, unsupported or failed operations and measured local bytes reclaimed. Updating this skill alone does not delete chats, caches or installers.
