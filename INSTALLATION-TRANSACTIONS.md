# Native installation transactions

This is the implementation reference for atomic file replacement and rollback during native MAKO Renderer installation. It covers MAKO Decky and the standalone archive installer so agents and maintainers can change either path without weakening the shared installation contract. User instructions remain in the [Renderer README](engine/README.md#installation) and [Decky README](plugin/README.md#what-it-manages).

## Contract and limits

Both installers prepare replacement files separately from their destination and retain previous files until the native installation succeeds. Each destination replacement is atomic; the complete installation is a sequence of replacements and removals with rollback. A reader can observe files from different generations during that sequence. `active-renderer.json` records the selected owner/version after the payload work; it is not a filesystem-wide commit pointer used to gate all readers.

| Property | MAKO Decky | Standalone archive |
| --- | --- | --- |
| Replacement primitive | Adjacent staged file, then `Path.replace()` | Adjacent `replacement` file, then `mv -fT` |
| Previous regular file | Adjacent hard link, falling back to `shutil.copy2()` | Adjacent `cp -Pp` copy |
| Previous symlink | Recreate the link itself in the backup directory | Preserve the link itself with `cp -Pp` |
| Failure recovery | Catch `BaseException` inside `managed_install_transaction()` and restore in reverse order | `EXIT` cleanup restores in reverse order while replacing and not committed; `HUP`, `INT`, and `TERM` exit through that cleanup |
| Native success boundary | Normal exit from the transaction context after both identity writes | `install_committed=1` after every destination operation |
| Configuration | `conf.toml` participates in rollback | Installation leaves configuration alone |
| Durability | Flush and `fsync()` each staged output before rename; no parent-directory fsync or durable transaction journal | No explicit fsync or durable transaction journal |

Neither path provides automatic recovery after power loss, `SIGKILL`, or another termination that prevents cleanup from running. Neither acquires a shared installation lock across the standalone process, Decky, and configuration writers. Run one installer at a time with games using MAKO closed and configuration edits stopped. Per-file replacement avoids truncating an existing mapped inode, but does not make concurrent installation or game launch safe.

The rollback set consists of explicit file paths. It does not restore the directory tree, undo unrelated writes, or coordinate with another process. Directories created during preparation can remain after failure. Temporary files and backups normally disappear on completion; cleanup errors are reported and can leave artifacts.

## Canonical owners

| Owner | Responsibility |
| --- | --- |
| [installation.py](plugin/py_modules/mako_plugin/installation.py) | `InstallationService.install()`, payload validation and selection, `_decky_renderer_files()`, configuration preparation, native identity and coexistence |
| [managed_files.py](plugin/py_modules/mako_plugin/managed_files.py) | Atomic copy/text replacement, permissions, backup acquisition, rollback and retained recovery backups |
| [base_service.py](plugin/py_modules/mako_plugin/base_service.py) and [constants.py](plugin/py_modules/mako_plugin/constants.py) | Decky user-home resolution and managed destination paths; schema-owned identities come from [shared_config.py](plugin/shared_config.py) |
| [configuration.py](plugin/py_modules/mako_plugin/configuration.py), [profile_storage.py](plugin/py_modules/mako_plugin/profile_storage.py), and [wrapper_generation.py](plugin/py_modules/mako_plugin/wrapper_generation.py) | Canonical profile/sidecar reads and wrapper generation consumed by installation |
| [plugin.py](plugin/py_modules/mako_plugin/plugin.py) | `install_mako()` RPC and the subsequent Flatpak refresh |
| [mako-installer](engine/scripts/mako-installer) | Standalone manifest validation, staging, destination preparation, commit, rollback and uninstall |
| [package-local.sh](engine/scripts/package-local.sh) | Standalone archive layout, version file, checksummed payload manifest, private/public Vulkan manifests and package verification |

These components are independently deployed. Keep their native implementations in their existing owners and their agreement in portable contract tests; do not introduce a runtime import between them.

## Shared native selection and identity

At the default user-local prefix, Decky stores libraries under `~/.local/share/mako-render/lib{,32}/`; the standalone archive stores them under `~/.local/lib{,32}/`. Both installers supply shared manifests under `~/.local/share/mako-render/vulkan/` and `~/.local/share/vulkan/implicit_layer.d/`, selecting the installed libraries for the launch workflows. Both binary locations can remain present after switching owner.

| Record | Meaning |
| --- | --- |
| `~/.local/share/mako-render/installed-engine.json` | Decky's installed archive name, version, SHA-256 and architecture descriptors; can describe an inactive Decky payload |
| `~/.local/share/mako-render/active-renderer.json` | Schema-1 selected native identity: `owner` is `decky` or `standalone`, with `version`; Decky also writes its archive `sha256hash` |
| `~/.local/share/mako-render/installer/installed-files.sha256` | Standalone relative-path ownership record, with hashes of installed content after desktop-entry rewriting |

`InstallationService._active_manifest_owner()` resolves the private Frame Generation manifest's library path to determine which managed payload it selects. `check_installation()` checks agreement with the selected owner, required files, host support and wrapper presence before reporting installation/update state; an identity file alone is insufficient. Legacy identity readers and their removal gates remain in [CLEANUPS.md](CLEANUPS.md).

Standalone `MAKO_INSTALL_PREFIX` relocates its payload and state together. Decky's coexistence logic targets the default `~/.local` installation. Do not assume a custom standalone prefix is adopted by Decky.

## MAKO Decky sequence

1. `install()` reads `package.json` through `_bundled_archive_metadata()`: either `bundled_renderer` or exactly one `remote_binary`, never both. It checks native host compatibility, archive existence and the archive SHA-256 before creating installation directories. Native AArch64/Armada remains subject to [ARMADA.md](plugin/docs/ARMADA.md).
2. `_ensure_directories()` prepares the destinations. `managed_install_transaction()` snapshots the deduplicated `_decky_renderer_files() + [config_file_path]` set before entering the installation body. A destination must be absent, a regular file, or a symlink; other existing types abort preparation.
3. `_extract_and_install_files()` stages selected regular archive members under `.mako-install-*` in MAKO's user-owned data directory. It maps recognized layer and shader-catalog paths to fixed destinations and recognizes the optional CLI by basename; it does not extract arbitrary archive paths into the installation. The required 64-bit FG/scaling chain and complete shader catalog must exist. The optional 32-bit chain must be complete or absent. Layer role and profile-fallback build markers are validated before the replacement loop.
4. That loop normalizes layer JSON and replaces binaries/manifests through the managed-file helpers. JSON parsing/normalization occurs in the loop, so a malformed later manifest can fail after earlier replacements and require rollback. A 64-bit-only local payload removes stale managed 32-bit layer files.
5. `_register_layer_manifests()` installs gated discovery manifests and removes stale 32-bit registration when absent. The Gamescope WSI and guarded postprocess helpers refresh their managed files. An unavailable or invalid optional host integration removes its managed copy and leaves that integration unavailable; this alone is not a native-install failure. Filesystem errors that escape those helpers trigger rollback.
6. `_create_config_file()` preserves and merges valid profiles with current defaults. An unreadable/unparseable/invalid configuration, including an unsupported version, falls back to defaults; a file with no write bits is explicitly rejected before that fallback. The canonical schema removes unknown keys on serialization. Because `conf.toml` is in the transaction, a later install failure restores the original file even after a default fallback.
7. `_create_mako_launch_script()` rebuilds `mako-run` from the resulting configuration and existing sidecar reads. `_install_diagnostics_helper()` installs the packaged helper. Wrapper/profile sidecars are not installation outputs and are not in this rollback set.
8. `_write_engine_state()` writes the Decky payload record; `_write_active_renderer_state()` writes the selected Decky identity last. Normal context exit discards backups and `install()` reports success. Exceptions cause restoration before the service returns its failure response; non-`Exception` failures are re-raised after context cleanup.

`_decky_renderer_files()` is the exact file-set owner: it includes both layer chains, private and registered manifests, managed Gamescope/MangoHud/vkBasalt files, the packaged vkBasalt shader catalog, the optional CLI, both identity records, `mako-run`, and `mako-diagnostics`. Add any new file written or removed by the installation body to that set before relying on rollback. Standalone binaries and its ownership manifest are outside this set; shared entries overwritten by Decky are inside it. Keeping the packaged catalog under `~/.local/share/mako-render/vkbasalt-shaders/` also preserves configs written by older Qt builds, while current Decky and Qt saves use the canonical per-user copies under `~/.config/mako-render/vkbasalt/shaders/`.

### File replacement and backup mechanics

`_create_staged_file()` creates an exclusive random sibling with `O_CREAT | O_EXCL` and the requested mode, plus `O_CLOEXEC` when available. Requested modes are normally `0644` for data and `0755` for executables. A restrictive umask is retained when required owner bits survive; if owner bits are missing, `fchmod()` attempts to restore the requested mode and failure aborts the write.

The copy/text helpers finish writing, flush and fsync the staged file, close it, then replace the destination entry. An existing destination symlink is replaced without writing through it. Parent directories must permit creation and rename even when the old managed file itself is read-only. `write_managed_text_atomically()` skips replacement when content matches, required owner bits match, and the existing regular file has no permissions outside the requested mode; private umasks therefore do not cause repeated rewrites.

For each existing transaction path, `managed_install_transaction()` allocates an adjacent `.mako-rollback-*` directory. A regular file is hard-linked into it, with a copy fallback; a symlink is backed up as a symlink. An absent path is recorded without a backup file. All callers must replace existing inodes rather than edit them in place, because an in-place write would also change a hard-linked backup.

On failure, the helper restores backups with `Path.replace()` in reverse path order and unlinks paths that were originally absent. It attempts every restoration even if one fails. A failed restoration retains its available backup and raises an `OSError` containing the original error, restoration failures and retained directories. Backup cleanup failures are warnings and do not reverse a successful install.

## Standalone sequence

1. `install_payload()` checks for the packaged manifest/version, warns if Decky is installed, validates the version and relative-path/hash syntax of the new manifest and any existing ownership manifest, and obtains the installer's normal confirmation.
2. It installs `EXIT`, `HUP`, `INT`, and `TERM` traps and stages under `$install_prefix/.mako-install-*`. Every manifest entry is copied with preserved mode and checked against its SHA-256 before any selected destination is replaced.
3. Desktop `Exec` paths are rewritten in staging. The installer computes `installed-files.sha256` from the rewritten staged content and creates the standalone `active-renderer.json` there.
4. `prepare_install_destination()` prepares every payload destination with an adjacent `.mako-install-*` directory containing `previous` when the destination existed and `replacement` when new content will be installed. Previous symlinks are copied as links. This finishes for all destinations before `install_replacing=1`.
5. An old ownership entry absent from the new manifest is scheduled for removal only when its current file still matches the old hash. Modified obsolete files are preserved. Current payload paths are replacement targets even if their previous contents were modified. The ownership manifest and active identity are prepared last, in that order.
6. After `install_replacing=1`, the destination loop performs `mv -fT` for replacements and `rm -f` for scheduled removals. Only after the complete loop does it set `install_committed=1` and invoke `finish_install()` to discard staging/backups and remove traps.
7. Completion notification and optional `mako-ui` startup occur after commit. A configuration-app startup failure is reported separately and does not roll back the installed Renderer.

Before replacement starts, failure only needs staging cleanup. During replacement, `finish_install()` restores every prepared destination in reverse order from `previous`, or removes it if originally absent. Restoration failures retain available `previous` backups beside the destination and report their locations; a failure removing an originally absent path has no previous file to retain. The installer returns failure when the native transaction fails.

## Boundaries outside the transaction

- `Plugin.install_mako()` refreshes already installed Flatpak extensions only after native installation succeeds. A reported refresh failure leaves native success intact and adds `flatpak_refresh_error`; individual runtime updates are not rolled back with the native files. [Flatpak preparation](engine/docs/FLATPAK-GUIDE.md) is a separate boundary.
- Decky Loader's ZIP installation, plugin startup repairs, profile-edit transactions, direct development deployment, manual archive extraction, `cmake --install`, and native uninstallation do not pass through these complete native-install transactions. An atomic helper used by one of those paths does not imply the surrounding operation has this rollback contract.
- Configuration/profile sidecars, diagnostics history, installed Flatpak extensions, the plugin itself, and the licensed `Lossless.dll` are outside the native payload rollback set. Decky's `conf.toml` exception is described above; standalone configuration removal is an explicit uninstall option.
- Adjacent backup naming supports error reporting and manual recovery, not automatic replay. There is no persistent operation journal or startup scan that can safely infer and complete an interrupted installation from leftover directories. Preserve reported backups when investigating restoration failures.

## Validation and maintenance

| Portable evidence | Covered behavior |
| --- | --- |
| [test_dual_arch_installation.py](plugin/tests/test_dual_arch_installation.py) | Archive checksum/host/marker rejection, complete optional 32-bit chain, manifest rewriting, shared-owner selection, file modes and symlinks, failed copy preservation, reverse restoration of deleted/new files, retained recovery backups, configuration fallback/rollback, restrictive-umask no-op and late install failure |
| [test_plugin_installation.py](plugin/tests/test_plugin_installation.py) | Native-install/Flatpak-refresh result handling, including skipping refresh after native failure and retaining native success after a reported refresh failure |
| [test-mako-installer.sh](engine/scripts/test-mako-installer.sh) | Manifest/desktop rewriting, corrupt late payload before replacement, injected final identity-rename failure, restored payload/ownership/identity, new-file removal, staging cleanup, permission failures, successful retry and post-commit UI failure; registered as CTest `standalone-installer` |
| [test_package_contract.py](plugin/tests/test_package_contract.py) and [test_path_package_contract.py](plugin/tests/test_path_package_contract.py) | Package identities/layout and cross-component managed paths |

Run the focused installer checks from the repository root:

```bash
(cd plugin && python3 -m unittest discover -s tests -p 'test_*installation.py')
bash engine/scripts/test-mako-installer.sh
```

These use temporary files and synthetic payloads; they do not install into the user's active environment. Permission cases in the standalone script require a non-root test process. The suites exercise specific injected failures, not every syscall, signal timing, disk-full condition, filesystem or crash-recovery scenario.

Follow [TESTING.md](TESTING.md) for the broader portable gates and package/hardware evidence proportional to an implementation change. When moving this contract, update the owning installer/helper, its file set or packaging generator, focused failure coverage, and this reference together. Preserve existing compatibility identifiers and update [CLEANUPS.md](CLEANUPS.md) only if a compatibility path changes.
