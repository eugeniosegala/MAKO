#!/usr/bin/env bash
# Update an installed local Decky plugin without rebuilding a release ZIP.
set -euo pipefail

project_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
repository_root="$(cd "$project_dir/.." && pwd)"
flatpak_runtime_version_output="$(
  python3 "$project_dir/scripts/read_flatpak_runtime_contract.py" versions
)"
flatpak_runtime_bundle_output="$(
  python3 "$project_dir/scripts/read_flatpak_runtime_contract.py" bundles
)"
renderer_path_output="$(
  python3 "$project_dir/scripts/read_flatpak_runtime_contract.py" renderer-paths
)"
flatpak_runtime_versions=()
while IFS= read -r runtime_version; do
  [[ -n "$runtime_version" ]] && flatpak_runtime_versions+=("$runtime_version")
done <<< "$flatpak_runtime_version_output"
flatpak_runtime_bundles=()
while IFS= read -r runtime_bundle; do
  [[ -n "$runtime_bundle" ]] && flatpak_runtime_bundles+=("$runtime_bundle")
done <<< "$flatpak_runtime_bundle_output"
renderer_paths=()
while IFS= read -r renderer_path; do
  [[ -n "$renderer_path" ]] && renderer_paths+=("$renderer_path")
done <<< "$renderer_path_output"
if ((${#flatpak_runtime_versions[@]} == 0 ||
      ${#flatpak_runtime_versions[@]} != ${#flatpak_runtime_bundles[@]})); then
  echo "The shared Flatpak runtime contract is empty or inconsistent." >&2
  exit 1
fi
if ((${#renderer_paths[@]} != 3)); then
  echo "The shared Renderer path contract is incomplete." >&2
  exit 1
fi
renderer_library_filename="${renderer_paths[0]}"
renderer_library_relative_path="${renderer_paths[1]}"
renderer_library32_relative_path="${renderer_paths[2]}"
flatpak_runtime_summary="$(
  python3 "$project_dir/scripts/read_flatpak_runtime_contract.py" summary
)"
if [[ -z "$flatpak_runtime_summary" ]]; then
  echo "The shared Flatpak runtime summary is empty." >&2
  exit 1
fi
plugin_dir="${DECKY_PLUGIN_DIR:-$HOME/homebrew/plugins/Mako}"
engine_repo="${MAKO_ENGINE_REPO:-$project_dir/../engine}"
deploy_frontend=false
deploy_backend=false
deploy_engine=false
deploy_engine_32=false
deploy_flatpaks=false
reload_plugin=false
action_selected=false

usage() {
  cat <<EOF
Usage: scripts/deploy-dev.sh [options]

Updates an already installed local MAKO Decky plugin in place.
With no options it rebuilds and deploys the Decky frontend and Python backend.
It never builds a ZIP, downloads release payloads, publishes anything, or runs
the full test suite. Flatpak options build local development extensions.

Options:
  --frontend              Rebuild and deploy dist/ with fresh dev-build metadata.
  --backend               Regenerate and deploy Python backend plus refreshed dev UI.
  --engine                Incrementally build/deploy the 64-bit host layer plus refreshed dev UI.
  --engine-32             Incrementally build/deploy the 32-bit host layer plus refreshed dev UI.
  --host                  Deploy Decky plus both 64-bit and 32-bit host layers.
  --flatpaks              Deploy Decky plus Flatpak runtimes $flatpak_runtime_summary.
  --e2e                   Deploy Decky, both host layers, and all Flatpak runtime bundles.
  --all                   Deploy frontend, backend, and engine.
  --reload                Reload only this plugin through Decky after deployment.
  --plugin-dir PATH       Installed Decky plugin directory.
  --engine-repo PATH      MAKO Renderer source directory for --engine.
  -h, --help              Show this help.

The plugin must first have installed its engine normally. Quit the test game
before --engine. Reload it from Decky's Developer menu after deployment, or
pass --reload to reload only this plugin automatically.
EOF
}

while (($#)); do
  case "$1" in
    --frontend)
      deploy_frontend=true
      action_selected=true
      ;;
    --backend)
      deploy_backend=true
      action_selected=true
      ;;
    --engine)
      deploy_engine=true
      action_selected=true
      ;;
    --engine-32)
      deploy_engine_32=true
      action_selected=true
      ;;
    --host)
      deploy_frontend=true
      deploy_backend=true
      deploy_engine=true
      deploy_engine_32=true
      action_selected=true
      ;;
    --flatpaks)
      deploy_frontend=true
      deploy_backend=true
      deploy_flatpaks=true
      action_selected=true
      ;;
    --e2e)
      deploy_frontend=true
      deploy_backend=true
      deploy_engine=true
      deploy_engine_32=true
      deploy_flatpaks=true
      action_selected=true
      ;;
    --all)
      deploy_frontend=true
      deploy_backend=true
      deploy_engine=true
      action_selected=true
      ;;
    --reload)
      reload_plugin=true
      action_selected=true
      ;;
    --plugin-dir)
      if (($# < 2)); then
        echo "--plugin-dir requires a path" >&2
        exit 2
      fi
      plugin_dir="$2"
      shift
      ;;
    --engine-repo)
      if (($# < 2)); then
        echo "--engine-repo requires a path" >&2
        exit 2
      fi
      engine_repo="$2"
      shift
      ;;
    -h|--help)
      usage
      exit 0
      ;;
    *)
      echo "Unknown option: $1" >&2
      usage >&2
      exit 2
      ;;
  esac
  shift
done

if [[ "$action_selected" == false ]]; then
  deploy_frontend=true
  deploy_backend=true
fi

# Every direct development deployment refreshes the visible status box. An
# engine-only or backend-only run therefore also rebuilds the small frontend.
if [[ "$deploy_backend" == true || "$deploy_engine" == true ||
      "$deploy_engine_32" == true || "$deploy_flatpaks" == true ]]; then
  deploy_frontend=true
fi

if [[ "$deploy_flatpaks" == true ]] && ! command -v flatpak-builder >/dev/null 2>&1; then
  echo "Flatpak runtime packaging needs flatpak-builder." >&2
  echo "On SteamOS, install it with:" >&2
  echo "  sudo steamos-readonly disable" >&2
  echo "  sudo pacman -S flatpak-builder" >&2
  echo "  sudo steamos-readonly enable" >&2
  exit 1
fi

if [[ "$plugin_dir" != /* ]]; then
  plugin_dir="$PWD/$plugin_dir"
fi
if [[ "$engine_repo" != /* ]]; then
  engine_repo="$PWD/$engine_repo"
fi

if [[ ! -f "$plugin_dir/plugin.json" ]]; then
  echo "Installed Decky plugin not found: $plugin_dir" >&2
  echo "Install this plugin once through Decky, or pass --plugin-dir PATH." >&2
  exit 1
fi

plugin_name="$(python3 -c '
import json
import sys
with open(sys.argv[1], encoding="utf-8") as source:
    print(json.load(source).get("name", ""))
' "$plugin_dir/plugin.json")"
if [[ "$plugin_name" != "MAKO Decky" &&
      "$plugin_name" != "MAKO" && "$plugin_name" != "Mako" &&
      "$plugin_name" != "MAKO - Frame Generation" ]]; then
  echo "Refusing to modify a different Decky plugin: $plugin_dir" >&2
  exit 1
fi

copy_file() {
  local source_path="$1"
  local destination_path="$2"
  local destination_dir
  local temporary_path

  if [[ -f "$destination_path" ]] && cmp -s "$source_path" "$destination_path"; then
    return
  fi

  destination_dir="$(dirname "$destination_path")"
  mkdir -p "$destination_dir"
  if [[ -w "$destination_dir" ]]; then
    temporary_path="$(mktemp "$destination_dir/.${destination_path##*/}.XXXXXX")"
    cp "$source_path" "$temporary_path"
    chmod --reference="$source_path" "$temporary_path"
    mv -f "$temporary_path" "$destination_path"
  elif [[ -f "$destination_path" && -w "$destination_path" ]]; then
    # Decky's installer can leave the plugin root owned by root while handing
    # individual payload files to the deck user. Rename-based atomic updates
    # need directory write permission, so replace only that writable file.
    echo "Replacing writable file in a protected Decky directory: $destination_path"
    cp "$source_path" "$destination_path"
    chmod --reference="$source_path" "$destination_path"
  else
    echo "Cannot update $destination_path; neither its directory nor file is writable." >&2
    exit 1
  fi
}

built_layer_64=""
built_layer_32=""
installed_layer_64=""
installed_layer_32=""
flatpak_archive=""
flatpak_unpack_dir=""
cleanup() {
  if [[ -n "$flatpak_unpack_dir" ]]; then
    rm -rf "$flatpak_unpack_dir"
  fi
}
trap cleanup EXIT

if [[ "$deploy_engine" == true || "$deploy_engine_32" == true ]]; then
  if [[ ! -x "$engine_repo/scripts/build-steamos-dev.sh" ]]; then
    echo "Incremental engine builder not found: $engine_repo/scripts/build-steamos-dev.sh" >&2
    exit 1
  fi

  engine_build_args=()
  if [[ "$deploy_engine" == true && "$deploy_engine_32" == true ]]; then
    engine_build_args+=(--with-32-bit)
  elif [[ "$deploy_engine_32" == true ]]; then
    engine_build_args+=(--32-bit-only)
  fi
  "$engine_repo/scripts/build-steamos-dev.sh" "${engine_build_args[@]}"

  engine_build_dir="${MAKO_BUILD_DIR:-$engine_repo/build/steamos-dev}"
  if [[ "$engine_build_dir" != /* ]]; then
    engine_build_dir="$engine_repo/$engine_build_dir"
  fi
  if [[ "$deploy_engine" == true ]]; then
    built_layer_64="$engine_build_dir/mako-render/$renderer_library_filename"
    installed_layer_64="$HOME/$renderer_library_relative_path"
  fi
  if [[ "$deploy_engine_32" == true ]]; then
    engine_build_32_dir="${MAKO_BUILD_32_DIR:-${engine_build_dir}-32}"
    if [[ "$engine_build_32_dir" != /* ]]; then
      engine_build_32_dir="$engine_repo/$engine_build_32_dir"
    fi
    built_layer_32="$engine_build_32_dir/mako-render/$renderer_library_filename"
    installed_layer_32="$HOME/$renderer_library32_relative_path"
  fi
  for layer_path in "$built_layer_64" "$built_layer_32"; do
    if [[ -n "$layer_path" && ! -f "$layer_path" ]]; then
      echo "Incremental engine build did not produce: $layer_path" >&2
      exit 1
    fi
  done
  for installed_path in "$installed_layer_64" "$installed_layer_32"; do
    if [[ -n "$installed_path" && ! -f "$installed_path" ]]; then
      echo "MAKO Renderer is not installed yet: $installed_path" >&2
      echo "Use MAKO Decky's 'Install MAKO Renderer' action once before deploying host layers." >&2
      exit 1
    fi
  done
fi

if [[ "$deploy_flatpaks" == true ]]; then
  if [[ ! -x "$engine_repo/scripts/package-flatpaks.sh" ]]; then
    echo "Flatpak packager not found: $engine_repo/scripts/package-flatpaks.sh" >&2
    exit 1
  fi
  engine_version="$(tr -d '[:space:]' < "$engine_repo/VERSION")"
  flatpak_archive="$engine_repo/out/MAKO-Renderer-v$engine_version-steamos-dev-flatpaks.tar.xz"
  build_cache_root="${MAKO_BUILD_CACHE_ROOT:-$engine_repo/build/cache}"
  build_work_root="${MAKO_BUILD_WORK_ROOT:-$engine_repo/build/work}"
  flatpak_cache_root="${MAKO_FLATPAK_CACHE_ROOT:-$build_cache_root/flatpak}"
  flatpak_tmp_root="${MAKO_FLATPAK_TMP_ROOT:-$build_work_root/flatpak}"
  if [[ "$flatpak_cache_root" != /* ]]; then
    flatpak_cache_root="$engine_repo/$flatpak_cache_root"
  fi
  if [[ "$flatpak_tmp_root" != /* ]]; then
    flatpak_tmp_root="$engine_repo/$flatpak_tmp_root"
  fi
  mkdir -p "$flatpak_tmp_root"
  echo "Building all MAKO Flatpak runtime bundles..."
  TMPDIR="$flatpak_tmp_root" MAKO_FLATPAK_WORK_ROOT="$flatpak_tmp_root" \
    MAKO_FLATPAK_CACHE_ROOT="$flatpak_cache_root" \
    "$engine_repo/scripts/package-flatpaks.sh" "$flatpak_archive"
  flatpak_unpack_dir="$(mktemp -d "${TMPDIR:-/tmp}/mako-flatpaks.XXXXXX")"
  tar -xJf "$flatpak_archive" -C "$flatpak_unpack_dir"
  for ((runtime_index = 0; runtime_index < ${#flatpak_runtime_versions[@]}; runtime_index++)); do
    runtime_version="${flatpak_runtime_versions[$runtime_index]}"
    flatpak_bundle="$flatpak_unpack_dir/${flatpak_runtime_bundles[$runtime_index]}"
    if [[ ! -s "$flatpak_bundle" ]]; then
      echo "Flatpak archive is missing runtime $runtime_version: $flatpak_bundle" >&2
      exit 1
    fi
  done
fi

dev_build_info_path="$project_dir/.dev-build-info.json"
dev_build_info_args=(
  --output "$dev_build_info_path"
  --plugin-repo "$project_dir"
  --frontend-deployed "$deploy_frontend"
  --backend-deployed "$deploy_backend"
)
if [[ -n "$built_layer_64" || -n "$built_layer_32" || -n "$flatpak_archive" ]]; then
  dev_build_info_args+=(
    --engine-repo "$engine_repo"
  )
  if [[ -n "$built_layer_64" ]]; then
    dev_build_info_args+=(--engine-layer-64 "$built_layer_64")
  fi
  if [[ -n "$built_layer_32" ]]; then
    dev_build_info_args+=(--engine-layer-32 "$built_layer_32")
  fi
  if [[ -n "$flatpak_archive" ]]; then
    dev_build_info_args+=(--flatpak-archive "$flatpak_archive")
  fi
fi
node "$project_dir/scripts/generate-dev-build-info.mjs" "${dev_build_info_args[@]}"

# Direct development deployments use the same runtime flag as local ZIPs.
# Copy only this generated flavour override; published packages retain the
# tracked default from py_modules/mako_plugin/build_flavor.py.
copy_file "$project_dir/defaults/build_flavor.dev.py" \
  "$plugin_dir/py_modules/mako_plugin/build_flavor.py"

if [[ "$deploy_frontend" == true || "$deploy_backend" == true ]]; then
  echo "Generating configuration bindings..."
  python3 "$project_dir/scripts/generate_ts_schema.py"
fi

if [[ "$deploy_frontend" == true ]]; then
  echo "Building Decky frontend..."
  (
    cd "$project_dir"
    MAKO_LOCAL_RELEASE_BUILD=1 \
      MAKO_DEV_BUILD_INFO_PATH="$dev_build_info_path" \
      node "$project_dir/scripts/build-frontend.mjs"
  )
  copy_file "$project_dir/dist/index.js" "$plugin_dir/dist/index.js"
  if cmp -s "$project_dir/plugin.json" "$plugin_dir/plugin.json"; then
    :
  elif [[ -w "$plugin_dir" || -w "$plugin_dir/plugin.json" ]]; then
    copy_file "$project_dir/plugin.json" "$plugin_dir/plugin.json"
  else
    echo "Skipped protected Decky manifest; reinstall the next ZIP to apply manifest changes."
  fi
  if [[ -f "$project_dir/dist/index.js.map" ]]; then
    copy_file "$project_dir/dist/index.js.map" "$plugin_dir/dist/index.js.map"
  fi
  echo "Deployed Decky frontend."
fi

if [[ "$deploy_backend" == true ]]; then
  echo "Deploying Python backend..."
  copy_file "$project_dir/main.py" "$plugin_dir/main.py"
  copy_file "$project_dir/shared_config.py" "$plugin_dir/shared_config.py"
  copy_file "$repository_root/scripts/mako-diagnostics" \
    "$plugin_dir/bin/mako-diagnostics"
  cp -a "$project_dir/py_modules/." "$plugin_dir/py_modules/"
  copy_file "$project_dir/defaults/build_flavor.dev.py" \
    "$plugin_dir/py_modules/mako_plugin/build_flavor.py"
  find "$plugin_dir/py_modules" -type f \( -name '*.pyc' -o -name '*.pyo' \) -delete
  find "$plugin_dir/py_modules" -type d -name '__pycache__' -prune -exec rm -rf {} +
  echo "Deployed Decky Python backend and diagnostics helper."
fi

if [[ -n "$built_layer_64" ]]; then
  copy_file "$built_layer_64" "$installed_layer_64"
  echo "Deployed incremental 64-bit engine layer."
fi
if [[ -n "$built_layer_32" ]]; then
  copy_file "$built_layer_32" "$installed_layer_32"
  echo "Deployed incremental 32-bit engine layer."
fi
if [[ -n "$flatpak_archive" ]]; then
  for flatpak_bundle in "${flatpak_runtime_bundles[@]}"; do
    copy_file "$flatpak_unpack_dir/$flatpak_bundle" "$plugin_dir/bin/$flatpak_bundle"
  done
  echo "Deployed Flatpak runtime bundles $flatpak_runtime_summary."
fi

if [[ "$reload_plugin" == true ]]; then
  node "$project_dir/scripts/reload-decky-plugin.mjs" "$plugin_name"
else
  echo "Reload MAKO Decky from Decky's Developer menu before testing."
fi
