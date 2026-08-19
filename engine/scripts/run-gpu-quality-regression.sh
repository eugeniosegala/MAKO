#!/usr/bin/env bash
set -euo pipefail

if (($# < 2 || $# > 4)); then
    echo "Usage: $0 <mako-cli> <output-directory> [AUTO|REQUIRED|OFF] [skip-return-code]" >&2
    exit 2
fi

cli_path="$1"
output_directory="$2"
mode="${3:-AUTO}"
skip_return_code="${4:-0}"
mode="$(printf '%s' "$mode" | tr '[:lower:]' '[:upper:]')"
host_os="$(uname -s)"

if [[ "$mode" != "AUTO" && "$mode" != "REQUIRED" && "$mode" != "OFF" ]]; then
    echo "GPU quality mode must be AUTO, REQUIRED, or OFF: $mode" >&2
    exit 2
fi
if ! [[ "$skip_return_code" =~ ^[0-9]+$ ]] || ((skip_return_code > 255)); then
    echo "Skip return code must be between 0 and 255: $skip_return_code" >&2
    exit 2
fi

steam_machine=false
steam_machine_override="$(printf '%s' "${MAKO_STEAM_MACHINE:-}" | tr '[:upper:]' '[:lower:]')"
if [[ "$steam_machine_override" == "1" || "$steam_machine_override" == "true" ||
        "$steam_machine_override" == "yes" || "$steam_machine_override" == "on" ]]; then
    steam_machine=true
fi

if [[ "$host_os" == "Linux" && "$steam_machine" != true ]]; then
    release_paths=(/etc/os-release /usr/lib/os-release /etc/steamos-release)
    for release_path in "${release_paths[@]}"; do
        [[ -r "$release_path" ]] || continue
        if grep -Eiq 'SteamOS|^[[:space:]]*ID[[:space:]]*=[[:space:]]*"?steamos"?[[:space:]]*$' "$release_path"; then
            steam_machine=true
            break
        fi
    done
fi

if [[ "$host_os" == "Linux" && "$steam_machine" != true ]]; then
    dmi_product_name="/sys/class/dmi/id/product_name"
    if [[ -r "$dmi_product_name" ]] && grep -Eiq 'Steam[[:space:]-]*(Machine|Deck)' "$dmi_product_name"; then
        steam_machine=true
    fi
fi

if [[ "$mode" == "AUTO" && "$steam_machine" == true ]]; then
    mode="REQUIRED"
    echo "Steam Machine detected; AMD GPU image-quality regression is mandatory."
fi

skip_test() {
    local reason="$1"
    if [[ "$mode" == "REQUIRED" ]]; then
        echo "AMD GPU image-quality regression required but unavailable: $reason" >&2
        exit 1
    fi
    echo "AMD GPU image-quality regression skipped: $reason"
    exit "$skip_return_code"
}

if [[ "$mode" == "OFF" ]]; then
    skip_test "disabled by configuration"
fi
if [[ "$host_os" != "Linux" ]]; then
    skip_test "requires Linux Vulkan external-memory support"
fi
if [[ ! -x "$cli_path" ]]; then
    skip_test "mako-cli is not executable at $cli_path"
fi

amd_gpu_found=false
gpu_vendor_observed=false
vendor_paths=(
    /sys/class/drm/card*/device/vendor
    /sys/class/drm/renderD*/device/vendor
)
for vendor_path in "${vendor_paths[@]}"; do
    [[ -r "$vendor_path" ]] || continue
    gpu_vendor_observed=true
    read -r vendor_id < "$vendor_path"
    vendor_id="$(printf '%s' "$vendor_id" | tr '[:upper:]' '[:lower:]')"
    if [[ "$vendor_id" == "0x1002" ]]; then
        amd_gpu_found=true
        break
    fi
done

if [[ "$amd_gpu_found" != true ]] && command -v vulkaninfo >/dev/null 2>&1; then
    gpu_vendor_observed=true
    if vulkaninfo --summary 2>/dev/null | grep -Eiq 'AMD|Advanced Micro Devices|vendorID[[:space:]]*=[[:space:]]*0x1002'; then
        amd_gpu_found=true
    fi
fi
if [[ "$amd_gpu_found" != true ]]; then
    if [[ "$gpu_vendor_observed" == true ]]; then
        skip_test "no AMD Vulkan GPU was detected"
    fi
    skip_test "GPU vendor could not be detected"
fi

dll_path="${MAKO_QUALITY_DLL:-${MAKO_DLL_PATH:-}}"
if [[ -n "$dll_path" && ! -f "$dll_path" ]]; then
    skip_test "configured Lossless.dll does not exist: $dll_path"
fi
if [[ -z "$dll_path" ]]; then
    config_path="${MAKO_CONFIG:-${XDG_CONFIG_HOME:-${HOME:-}/.config}/mako-render/conf.toml}"
    if [[ -f "$config_path" ]]; then
        dll_path="$(sed -nE \
            -e 's/^[[:space:]]*dll[[:space:]]*=[[:space:]]*"([^"]+)".*$/\1/p' \
            -e "s/^[[:space:]]*dll[[:space:]]*=[[:space:]]*'([^']+)'.*$/\\1/p" \
            "$config_path" | head -n 1)"
        if [[ -n "$dll_path" && ! -f "$dll_path" ]]; then
            skip_test "Lossless.dll from $config_path does not exist: $dll_path"
        fi
    fi
fi
if [[ -z "$dll_path" ]]; then
    data_home="${XDG_DATA_HOME:-${HOME:-}/.local/share}"
    home_directory="${HOME:-}"
    dll_candidates=(
        "$data_home/Steam/steamapps/common/Lossless Scaling/Lossless.dll"
        "$home_directory/.local/share/Steam/steamapps/common/Lossless Scaling/Lossless.dll"
        "$home_directory/.steam/steam/steamapps/common/Lossless Scaling/Lossless.dll"
        "$home_directory/.steam/debian-installation/steamapps/common/Lossless Scaling/Lossless.dll"
        "$home_directory/.var/app/com.valvesoftware.Steam/.local/share/Steam/steamapps/common/Lossless Scaling/Lossless.dll"
        "$PWD/Lossless.dll"
    )
    for candidate in "${dll_candidates[@]}"; do
        if [[ -f "$candidate" ]]; then
            dll_path="$candidate"
            break
        fi
    done
fi
if [[ -z "$dll_path" ]]; then
    skip_test "Lossless.dll was not found; configure it in MAKO or set MAKO_QUALITY_DLL"
fi

mkdir -p "$output_directory/fp32" "$output_directory/fp16"

run_regression() {
    local precision="$1"
    local quality_command=(
        "$cli_path" quality-regression
        --dll "$dll_path"
        --output "$output_directory/$precision"
    )
    if [[ -n "${MAKO_QUALITY_GPU:-}" ]]; then
        quality_command+=(--gpu "$MAKO_QUALITY_GPU")
    fi
    if [[ "$precision" == "fp16" ]]; then
        quality_command+=(--allow-fp16)
    fi
    "${quality_command[@]}"
}

echo "Running AMD GPU image-quality regression (FP32)..."
run_regression fp32

echo "Running AMD GPU image-quality regression (FP16 allowed)..."
run_regression fp16

echo "AMD GPU image-quality regression passed in FP32 and FP16 modes."
