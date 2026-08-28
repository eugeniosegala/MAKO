#!/usr/bin/env bash
# Build a complete, manually installable Decky plugin archive.
set -euo pipefail

project_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
flatpak_runtime_bundle_output="$(
  python3 "$project_dir/scripts/read_flatpak_runtime_contract.py" bundles
)"
if [[ -z "$flatpak_runtime_bundle_output" ]]; then
  echo "The shared Flatpak runtime contract contains no bundles." >&2
  exit 1
fi
flatpak_runtime_bundles=()
while IFS= read -r runtime_bundle; do
  [[ -n "$runtime_bundle" ]] && flatpak_runtime_bundles+=("$runtime_bundle")
done <<< "$flatpak_runtime_bundle_output"
repository_root="$(cd "$project_dir/.." && pwd)"
output_path=""
output_path_set=false
engine_archive_path=""
flatpak_archive_path=""
local_engine_repo=""
local_engine_mode=false
local_plugin_mode=false
native_only=false
build_64_only=false
local_engine_commit=""
local_engine_dirty=false
local_engine_label=""
local_plugin_label=""
local_release_version=""

usage() {
  cat <<'EOF'
Usage: scripts/package-local.sh [options] [output-path]

Builds a verified Decky plugin ZIP for local installation. It never creates a
tag, pushes commits, or changes GitHub.

Options:
  --engine-archive PATH   Use a local engine archive instead of downloading it.
  --flatpak-archive PATH  Use a local Flatpak archive instead of downloading it.
  --local-engine-repo PATH
                          Build and bundle the engine checkout at PATH. Its
                          commit and generated checksums are recorded only in
                          the ZIP; tracked package.json is not changed.
  --local-plugin          Build a uniquely versioned plugin-only test ZIP
                          using the pinned released engine and Flatpak bundles.
                          Use this for Decky or wrapper changes; it avoids a
                          needless native-engine rebuild.
  --native-only           With --local-engine-repo, omit Flatpak extensions.
                          This is the fast path for native Steam game testing,
                          never for a release package.
  --64-bit-only           With --local-engine-repo, build and bundle only the
                          64-bit host layer. Intended only for fast local tests.
  -h, --help              Show this help.
EOF
}

while (($#)); do
  case "$1" in
    --)
      shift
      if (($# > 1)); then
        echo "Only one output path may be specified" >&2
        usage >&2
        exit 2
      fi
      if (($# == 1)); then
        output_path="$1"
        output_path_set=true
        shift
      fi
      break
      ;;
    --publish)
      echo "Use scripts/publish-package.sh to publish a GitHub release." >&2
      exit 2
      ;;
    --engine-archive)
      if (($# < 2)); then
        echo "--engine-archive requires a path" >&2
        exit 2
      fi
      engine_archive_path="$2"
      shift 2
      continue
      ;;
    --flatpak-archive)
      if (($# < 2)); then
        echo "--flatpak-archive requires a path" >&2
        exit 2
      fi
      flatpak_archive_path="$2"
      shift 2
      continue
      ;;
    --local-engine-repo)
      if (($# < 2)); then
        echo "--local-engine-repo requires a path" >&2
        exit 2
      fi
      local_engine_repo="$2"
      shift 2
      continue
      ;;
    --local-plugin)
      local_plugin_mode=true
      shift
      continue
      ;;
    --native-only)
      native_only=true
      shift
      continue
      ;;
    --64-bit-only)
      build_64_only=true
      shift
      continue
      ;;
    --help|-h)
      usage
      exit 0
      ;;
    --*)
      echo "Unknown option: $1" >&2
      usage >&2
      exit 2
      ;;
    *)
      if [[ "$output_path_set" == true ]]; then
        echo "Only one output path may be specified" >&2
        usage >&2
        exit 2
      fi
      output_path="$1"
      output_path_set=true
      ;;
  esac
  shift
done

if [[ -n "$local_engine_repo" &&
      ( -n "$engine_archive_path" || -n "$flatpak_archive_path" ) ]]; then
  echo "--local-engine-repo cannot be combined with archive override options" >&2
  exit 2
fi
if [[ "$local_plugin_mode" == true && -n "$local_engine_repo" ]]; then
  echo "--local-plugin cannot be combined with --local-engine-repo" >&2
  exit 2
fi
if [[ "$native_only" == true && -z "$local_engine_repo" ]]; then
  echo "--native-only requires --local-engine-repo" >&2
  exit 2
fi
if [[ "$build_64_only" == true && -z "$local_engine_repo" ]]; then
  echo "--64-bit-only requires --local-engine-repo" >&2
  exit 2
fi

for command in curl node npm python3 strings tar zip unzip; do
  if ! command -v "$command" >/dev/null 2>&1; then
    echo "Required command not found: $command" >&2
    exit 1
  fi
done

if command -v sha256sum >/dev/null 2>&1; then
  checksum_command=(sha256sum)
elif command -v shasum >/dev/null 2>&1; then
  checksum_command=(shasum -a 256)
else
  echo "Required command not found: sha256sum or shasum" >&2
  exit 1
fi

worktree_fingerprint() {
  local repo="$1"
  local component_root
  component_root="$(cd "$repo" && pwd)"
  {
    # Keep engine and Decky identities component-scoped. A README or frontend
    # edit must not invalidate an already verified engine archive.
    git -C "$repo" diff --binary HEAD -- . || true
    while IFS= read -r -d '' untracked_path; do
      # Generated packages must not become part of the source identity.  Apart
      # from making the label depend on old artifacts, including out/ makes
      # every package invalidate the engine archive it just produced.
      case "$untracked_path" in
        out/*) continue ;;
      esac
      printf 'untracked:%s\0' "$untracked_path"
      "${checksum_command[@]}" "$component_root/$untracked_path"
    done < <(git -C "$repo" ls-files --others --exclude-standard -z -- .)
  } | "${checksum_command[@]}" | awk '{print substr($1, 1, 8)}'
}

component_commit() {
  git -C "$1" log -1 --format=%H -- .
}

read -r archive_name archive_version archive_url archive_checksum flatpak_archive_name flatpak_archive_url flatpak_archive_checksum < <(
  node -e '
    const manifest = require(process.argv[1]);
    const [binary] = manifest.remote_binary ?? [];
    if (!binary?.name || !binary?.version || !binary?.url || !binary?.sha256hash) {
      process.exitCode = 1;
      throw new Error("package.json must define one versioned, verified remote_binary entry");
    }
    if (!Array.isArray(binary.host_architectures) ||
        binary.host_architectures.length === 0 ||
        binary.host_architectures.some((architecture) =>
          !["x86_64", "aarch64"].includes(architecture))) {
      process.exitCode = 1;
      throw new Error("remote_binary must declare supported native host architectures");
    }
    const flatpak = binary.flatpak_bundle;
    if (flatpak && (!flatpak.name || !flatpak.url || !flatpak.sha256hash)) {
      process.exitCode = 1;
      throw new Error("flatpak_bundle must define name, url, and sha256hash when present");
    }
    process.stdout.write([
      binary.name,
      binary.version,
      binary.url,
      binary.sha256hash,
      flatpak?.name ?? "",
      flatpak?.url ?? "",
      flatpak?.sha256hash ?? "",
    ].join("\t") + "\n");
  ' "$project_dir/package.json"
)

if [[ -z "$local_engine_repo" &&
      "$archive_name" != MAKO-Renderer-v*-linux.tar.xz &&
      "$archive_name" != mako-render-v*-linux.tar.xz ]]; then
  echo "The tracked renderer pin predates the MAKO Renderer release track and cannot package the renamed Vulkan layer." >&2
  echo "Use --local-engine-repo for bootstrap builds, or publish render-v$archive_version and commit the generated plugin/package.json pin first." >&2
  exit 1
fi

if [[ -n "$local_engine_repo" ]]; then
  local_engine_mode=true
  if ! command -v git >/dev/null 2>&1; then
    echo "Local engine packaging requires command: git" >&2
    exit 1
  fi
  if [[ ! -d "$local_engine_repo" ]]; then
    echo "Local engine repository not found: $local_engine_repo" >&2
    exit 1
  fi
  local_engine_repo="$(cd "$local_engine_repo" && pwd)"
  for required_path in VERSION scripts/package-local.sh scripts/package-flatpaks.sh; do
    if [[ ! -f "$local_engine_repo/$required_path" ]]; then
      echo "Local engine repository is missing $required_path: $local_engine_repo" >&2
      exit 1
    fi
  done

  local_engine_version="$(tr -d '[:space:]' < "$local_engine_repo/VERSION")"
  if [[ "$local_engine_version" != "$archive_version" ]]; then
    echo "Local engine VERSION does not match Decky's configured engine line" >&2
    echo "Decky:  $archive_version" >&2
    echo "Engine: $local_engine_version" >&2
    exit 1
  fi
  local_engine_commit="$(component_commit "$local_engine_repo")"
  local_engine_short_commit="${local_engine_commit:0:7}"
  local_engine_label="$local_engine_short_commit"
  if [[ -n "$(git -C "$local_engine_repo" status --porcelain --untracked-files=normal -- .)" ]]; then
    local_engine_dirty=true
    local_engine_fingerprint="$(worktree_fingerprint "$local_engine_repo")"
    local_engine_label="$local_engine_label.dirty.$local_engine_fingerprint"
  fi

  # A local Decky build must have a plugin version distinct from the previous
  # package, otherwise Decky can keep the already-loaded Python backend and
  # leave an older generated launch wrapper in place. Include the Decky commit
  # and worktree diff as well as the engine source identity.
  local_plugin_commit="$(component_commit "$project_dir")"
  local_plugin_commit="${local_plugin_commit:0:8}"
  local_plugin_fingerprint="$(worktree_fingerprint "$project_dir")"
  local_plugin_label="$local_engine_label.$local_plugin_commit.$local_plugin_fingerprint"
  if [[ "$native_only" == true ]]; then
    local_plugin_label="$local_plugin_label.native-only"
  fi
  if [[ "$build_64_only" == true ]]; then
    local_plugin_label="$local_plugin_label.x86_64"
  fi

  engine_archive_suffix="linux"
  if [[ "$build_64_only" == true ]]; then
    engine_archive_suffix="linux.x86_64"
  fi
  engine_archive_path="$local_engine_repo/out/MAKO-Renderer-v$archive_version-local.$local_engine_label-$engine_archive_suffix.tar.xz"
  archive_name="$(basename "$engine_archive_path")"

  if [[ -s "$engine_archive_path" ]]; then
    echo "Reusing matching local engine archive $archive_name..."
  else
    echo "Building local engine checkout $local_engine_label..."
    engine_package_args=()
    if [[ "$build_64_only" == true ]]; then
      engine_package_args+=(--64-bit-only)
    fi
    "$local_engine_repo/scripts/package-local.sh" \
      "${engine_package_args[@]}" "$engine_archive_path"
  fi
  if [[ "$native_only" == true ]]; then
    echo "Skipping Flatpak extension builds for this native-only test package."
    flatpak_archive_path=""
    flatpak_archive_name=""
    flatpak_archive_checksum=""
  else
    flatpak_archive_path="$local_engine_repo/out/MAKO-Renderer-v$archive_version-local.$local_engine_label-flatpaks.tar.xz"
    flatpak_archive_name="$(basename "$flatpak_archive_path")"
    if [[ -s "$flatpak_archive_path" ]]; then
      echo "Reusing matching local Flatpak archive $flatpak_archive_name..."
    else
      "$local_engine_repo/scripts/package-flatpaks.sh" "$flatpak_archive_path"
    fi
  fi
fi

if [[ "$local_plugin_mode" == true ]]; then
  # Wrapper/UI-only test packages must still have a distinct version or Decky
  # can retain the old Python backend and generated launcher. Include both the
  # committed source identity and any uncommitted diff: a diff-only fingerprint
  # is e3b0c442 for every clean commit and is therefore not a cache buster.
  # The engine and Flatpak artifacts remain the pinned, verified release payloads.
  if ! command -v git >/dev/null 2>&1; then
    echo "Local plugin packaging requires command: git" >&2
    exit 1
  fi
  local_plugin_commit="$(component_commit "$project_dir")"
  local_plugin_commit="${local_plugin_commit:0:8}"
  local_plugin_label="wrapper.$local_plugin_commit.$(worktree_fingerprint "$project_dir")"
fi

if [[ "$local_engine_mode" == true || "$local_plugin_mode" == true ]]; then
  local_release_version="$(
    node "$project_dir/scripts/read-release-info.mjs" \
      "$project_dir/RELEASE_NOTES.md" "MAKO Decky" --version
  )"
fi

for local_archive in "$engine_archive_path" "$flatpak_archive_path"; do
  if [[ -n "$local_archive" && ! -f "$local_archive" ]]; then
    echo "Local archive not found: $local_archive" >&2
    exit 1
  fi
done

if [[ "$output_path_set" == false ]]; then
  if [[ "$local_engine_mode" == true || "$local_plugin_mode" == true ]]; then
    output_path="$project_dir/out/MAKO-Decky-local.$local_plugin_label.zip"
  else
    output_path="$project_dir/out/MAKO-Decky.zip"
  fi
fi

case "$output_path" in
  /*) ;;
  *) output_path="$project_dir/$output_path" ;;
esac

staging_dir="$(mktemp -d "${TMPDIR:-/tmp}/mako-plugin-package.XXXXXX")"
package_name="Mako"
package_dir="$staging_dir/$package_name"

cleanup() {
  rm -rf "$staging_dir"
}
trap cleanup EXIT

echo "Generating configuration bindings..."
python3 "$project_dir/scripts/generate_ts_schema.py"

echo "Testing launch-wrapper environment..."
npm --prefix "$project_dir" test

echo "Building frontend..."
if [[ "$local_engine_mode" == true || "$local_plugin_mode" == true ]]; then
  MAKO_LOCAL_RELEASE_BUILD=1 npm --prefix "$project_dir" run build
else
  env -u MAKO_LOCAL_RELEASE_BUILD npm --prefix "$project_dir" run build
fi

mkdir -p "$package_dir/bin" "$package_dir/dist" "$package_dir/py_modules"
cp "$repository_root/scripts/mako-diagnostics" \
  "$package_dir/bin/mako-diagnostics"
chmod 0755 "$package_dir/bin/mako-diagnostics"
if [[ -n "$engine_archive_path" ]]; then
  echo "Using local MAKO Renderer $archive_version payload..."
  cp "$engine_archive_path" "$package_dir/bin/$archive_name"
else
  echo "Downloading verified MAKO Renderer $archive_version payload..."
  curl --fail --location --silent --show-error "$archive_url" \
    --output "$package_dir/bin/$archive_name"
fi

actual_checksum="$(${checksum_command[@]} "$package_dir/bin/$archive_name" | awk '{print $1}')"
if [[ "$local_engine_mode" == true ]]; then
  archive_checksum="$actual_checksum"
elif [[ "$actual_checksum" != "$archive_checksum" ]]; then
  echo "Checksum mismatch for $archive_name" >&2
  echo "Expected: $archive_checksum" >&2
  echo "Actual:   $actual_checksum" >&2
  exit 1
fi

manifest_paths=(
  "./share/vulkan/implicit_layer.d/VkLayer_MAKO_render.json"
  "./share/vulkan/implicit_layer.d/VkLayer_MAKO_spatial_scaling.json"
)
if [[ "$build_64_only" != true ]]; then
  manifest_paths+=(
    "./share/vulkan/implicit_layer.d/VkLayer_MAKO_render.x86.json"
    "./share/vulkan/implicit_layer.d/VkLayer_MAKO_spatial_scaling.x86.json"
  )
fi
for manifest_path in "${manifest_paths[@]}"; do
  if ! tar -tf "$package_dir/bin/$archive_name" | grep -Fx "$manifest_path" >/dev/null; then
    echo "Engine archive is missing $manifest_path" >&2
    exit 1
  fi
  manifest_content="$(tar -xJOf "$package_dir/bin/$archive_name" "$manifest_path")"
  expected_manifest_identity='"name": "VK_LAYER_MAKO_render"'
  expected_manifest_enable='"ENABLE_MAKO": "1"'
  expected_manifest_disable='"DISABLE_MAKO": "1"'
  if [[ "$manifest_path" == *VkLayer_MAKO_spatial_scaling* ]]; then
    expected_manifest_identity='"name": "VK_LAYER_MAKO_spatial_scaling"'
    expected_manifest_enable='"ENABLE_MAKO_SPATIAL_SCALING": "1"'
    expected_manifest_disable='"DISABLE_MAKO_SPATIAL_SCALING": "1"'
  fi
  if [[ "$manifest_content" != *"$expected_manifest_identity"* ||
        "$manifest_content" != *"$expected_manifest_enable"* ||
        "$manifest_content" != *"$expected_manifest_disable"* ]]; then
    echo "Engine archive has invalid MAKO Renderer layer gating in $manifest_path" >&2
    exit 1
  fi
done

layer_binary_paths=(
  "./lib/libmako-render.so"
  "./lib/libmako-render-scaling.so"
)
if [[ "$build_64_only" != true ]]; then
  layer_binary_paths+=(
    "./lib32/libmako-render.so"
    "./lib32/libmako-render-scaling.so"
  )
fi
for layer_binary_path in "${layer_binary_paths[@]}"; do
  verification_binary="$staging_dir/$(basename "$(dirname "$layer_binary_path")")-libmako-render.so"
  tar -xJOf "$package_dir/bin/$archive_name" "$layer_binary_path" > "$verification_binary"
  expected_identity="VK_LAYER_MAKO_render"
  if [[ "$layer_binary_path" == *libmako-render-scaling.so ]]; then
    expected_identity="VK_LAYER_MAKO_spatial_scaling"
  fi
  if ! strings "$verification_binary" |
      grep -F "MAKO Renderer: render layer active; identity=$expected_identity; build=$archive_version" >/dev/null; then
    echo "Engine archive has no matching MAKO Renderer build marker in $layer_binary_path" >&2
    exit 1
  fi
  if ! strings "$verification_binary" | grep -F "MAKO_PROFILE_FALLBACK" >/dev/null; then
    echo "Engine archive lacks the profile-fallback wrapper protocol in $layer_binary_path" >&2
    exit 1
  fi
done

if [[ -n "$flatpak_archive_name" ]]; then
  if [[ -n "$flatpak_archive_path" ]]; then
    echo "Using local MAKO Flatpak extensions..."
    cp "$flatpak_archive_path" "$package_dir/bin/$flatpak_archive_name"
  else
    echo "Downloading verified MAKO Flatpak extensions..."
    curl --fail --location --silent --show-error "$flatpak_archive_url" \
      --output "$package_dir/bin/$flatpak_archive_name"
  fi

  actual_flatpak_checksum="$(${checksum_command[@]} "$package_dir/bin/$flatpak_archive_name" | awk '{print $1}')"
  if [[ "$local_engine_mode" == true ]]; then
    flatpak_archive_checksum="$actual_flatpak_checksum"
  elif [[ "$actual_flatpak_checksum" != "$flatpak_archive_checksum" ]]; then
    echo "Checksum mismatch for $flatpak_archive_name" >&2
    echo "Expected: $flatpak_archive_checksum" >&2
    echo "Actual:   $actual_flatpak_checksum" >&2
    exit 1
  fi

  tar -xJf "$package_dir/bin/$flatpak_archive_name" -C "$package_dir/bin"
  rm -f "$package_dir/bin/$flatpak_archive_name"

  for flatpak_bundle in "${flatpak_runtime_bundles[@]}"; do
    if [[ ! -s "$package_dir/bin/$flatpak_bundle" ]]; then
      echo "Flatpak bundle archive is missing $flatpak_bundle" >&2
      exit 1
    fi
  done
fi

echo "Assembling Decky archive..."
cp "$repository_root/LICENSE.md" "$package_dir/LICENSE.md"
cp "$project_dir/README.md" "$project_dir/main.py" \
  "$project_dir/package.json" "$project_dir/plugin.json" "$project_dir/shared_config.py" \
  "$package_dir/"
if [[ "$local_engine_mode" == true ]]; then
  node -e '
    const fs = require("node:fs");
    const manifestPath = process.argv[1];
    const manifest = JSON.parse(fs.readFileSync(manifestPath, "utf8"));
    const [binary] = manifest.remote_binary ?? [];
    if (!binary) throw new Error("package.json has no remote_binary entry");
    const [archiveName, engineVersion, archiveChecksum, sourceCommit,
      sourceDirty, sourceLabel, flatpakName, flatpakChecksum,
      localPluginLabel, build64Only, localReleaseVersion] = process.argv.slice(2);
    const localLabel = sourceLabel;
    binary.name = archiveName;
    binary.version = `${engineVersion}-local.${localLabel}`;
    binary.release_tag = `local-worktree-${localLabel}`;
    binary.source_commit = sourceCommit;
    binary.local_worktree_dirty = sourceDirty === "true";
    binary.sha256hash = archiveChecksum;
    binary.architectures = build64Only === "true" ? ["64"] : ["64", "32"];
    binary.host_architectures = ["x86_64"];
    // Decky Loader unconditionally downloads every remote_binary entry after
    // extracting a ZIP. Local packages already embed this verified archive,
    // so expose it only through MAKO-owned package metadata.
    delete binary.url;
    delete binary.flatpak_bundle;
    manifest.bundled_renderer = binary;
    delete manifest.remote_binary;
    delete manifest.remote_binary_bundling;
    manifest.version = `${localReleaseVersion}.local.${localPluginLabel}`;
    fs.writeFileSync(manifestPath, JSON.stringify(manifest, null, 2) + "\n");
  ' "$package_dir/package.json" "$archive_name" "$archive_version" \
    "$archive_checksum" "$local_engine_commit" "$local_engine_dirty" \
    "$local_engine_label" "$flatpak_archive_name" "$flatpak_archive_checksum" \
    "$local_plugin_label" "$build_64_only" "$local_release_version"
elif [[ "$local_plugin_mode" == true ]]; then
  node -e '
    const fs = require("node:fs");
    const manifestPath = process.argv[1];
    const localPluginLabel = process.argv[2];
    const localReleaseVersion = process.argv[3];
    const manifest = JSON.parse(fs.readFileSync(manifestPath, "utf8"));
    const [binary] = manifest.remote_binary ?? [];
    if (!binary) throw new Error("package.json has no remote_binary entry");
    // Keep wrapper-only test ZIPs self-contained for the same Decky contract
    // as local-engine packages.
    delete binary.url;
    delete binary.flatpak_bundle;
    manifest.bundled_renderer = binary;
    delete manifest.remote_binary;
    delete manifest.remote_binary_bundling;
    manifest.version = `${localReleaseVersion}.local.${localPluginLabel}`;
    fs.writeFileSync(manifestPath, JSON.stringify(manifest, null, 2) + "\n");
  ' "$package_dir/package.json" "$local_plugin_label" "$local_release_version"
fi
cp -R "$project_dir/dist/." "$package_dir/dist/"
cp -R "$project_dir/py_modules/." "$package_dir/py_modules/"
if [[ "$local_engine_mode" == true || "$local_plugin_mode" == true ]]; then
  cp "$project_dir/defaults/build_flavor.dev.py" \
    "$package_dir/py_modules/mako_plugin/build_flavor.py"
fi

package_contract_mode="release"
if [[ "$local_engine_mode" == true || "$local_plugin_mode" == true ]]; then
  package_contract_mode="local"
fi
echo "Validating Decky package contract..."
node "$project_dir/scripts/validate-package-contract.mjs" \
  "$package_dir" "$package_contract_mode"

# Python bytecode is host-version-specific and is regenerated on Steam OS.
find "$package_dir/py_modules" -type f \( -name '*.pyc' -o -name '*.pyo' \) -delete
find "$package_dir/py_modules" -type d -name '__pycache__' -prune -exec rm -rf {} +

mkdir -p "$(dirname "$output_path")"
rm -f "$output_path"
(
  cd "$staging_dir"
  zip -qr "$output_path" "$package_name"
)

unzip -t "$output_path" >/dev/null
echo "Created and verified: $output_path"
