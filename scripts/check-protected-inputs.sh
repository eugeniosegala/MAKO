#!/usr/bin/env bash
set -euo pipefail

repository_root="${1:-}"
if [[ -z "$repository_root" ]]; then
    repository_root="$(git rev-parse --show-toplevel)"
fi
repository_root="$(git -C "$repository_root" rev-parse --show-toplevel)"

snapshot_root="$(mktemp -d "${TMPDIR:-/tmp}/mako-protected-inputs.XXXXXX")"
cleanup() {
    rm -rf "$snapshot_root"
}
trap cleanup EXIT

# Export the index rather than reading the working tree so pre-commit checks the
# exact content about to be committed, including staged-only changes.
git -C "$repository_root" checkout-index --all --prefix="$snapshot_root/"

failure=0
report_failure() {
    local path="$1"
    local reason="$2"
    printf 'Protected-input violation: %s (%s)\n' "$path" "$reason" >&2
    failure=1
}

while IFS= read -r -d '' path; do
    lowercase_path="$(printf '%s' "$path" | LC_ALL=C tr '[:upper:]' '[:lower:]')"
    case "$lowercase_path" in
        *.dll|*.exe|*.msi|*.sys|*.drv|*.ocx|*.pdb|*.dmp|*.mdmp|*.core|*.so|*.dylib|*.a|*.lib|*.o|*.obj|*.bin|*.onnx|*.pt|*.pth|*.safetensors|*.weights|*.ckpt|*.tflite|*.dxbc|*.cso|*.spv|*.zip|*.7z|*.rar|*.tar|*.tgz|*.tar.gz|*.tar.xz)
            report_failure "$path" "protected binary, model, shader, dump, or archive extension"
            ;;
    esac

    candidate="$snapshot_root/$path"
    if [[ ! -f "$candidate" || -L "$candidate" ]]; then
        continue
    fi

    magic="$(LC_ALL=C od -An -v -tx1 -N8 "$candidate" | tr -d '[:space:]')"
    case "$magic" in
        4d5a*) report_failure "$path" "Windows PE/DOS executable signature" ;;
        7f454c46*) report_failure "$path" "ELF executable or shared-library signature" ;;
        44584243*) report_failure "$path" "raw DXBC shader signature" ;;
        03022307*) report_failure "$path" "raw SPIR-V shader signature" ;;
        504b0304*|504b0506*|504b0708*) report_failure "$path" "ZIP archive signature" ;;
        377abcaf271c*) report_failure "$path" "7z archive signature" ;;
        52617221*) report_failure "$path" "RAR archive signature" ;;
        1f8b*) report_failure "$path" "gzip archive signature" ;;
        fd377a585a00*) report_failure "$path" "xz archive signature" ;;
        425a68*) report_failure "$path" "bzip2 archive signature" ;;
    esac

    lfs_magic="$(LC_ALL=C od -An -v -tx1 -N42 "$candidate" | tr -d '[:space:]')"
    if [[ "$lfs_magic" == "76657273696f6e2068747470733a2f2f6769742d6c66732e6769746875622e636f6d2f737065632f7631" ]]; then
        report_failure "$path" "Git LFS pointer could reference an unreviewed remote payload"
    fi

    size="$(LC_ALL=C wc -c < "$candidate" | tr -d '[:space:]')"
    if (( size >= 262 )); then
        tar_magic="$(LC_ALL=C od -An -v -tx1 -j257 -N5 "$candidate" | tr -d '[:space:]')"
        if [[ "$tar_magic" == "7573746172" ]]; then
            report_failure "$path" "tar archive signature"
        fi
    fi
done < <(git -C "$repository_root" ls-files -z)

if (( failure != 0 )); then
    printf '%s\n' 'Protected-input gate failed. Keep licensed DLL/model content and generated runtime artifacts outside Git.' >&2
    exit 1
fi

printf '%s\n' 'Protected-input gate passed: the Git index contains no protected binary, model, shader, dump, or archive payloads.'
