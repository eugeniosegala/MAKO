#!/usr/bin/env bash
set -euo pipefail

repository_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$repository_root"

usage() {
  cat <<'EOF'
Usage: ./scripts/publish-release.sh X.Y.Z

Publishes the matching MAKO Renderer and MAKO Decky releases. The workflow is
resumable: a component whose tag, GitHub release, assets, version, and Renderer
pins are already complete is verified and skipped. Both component
RELEASE_NOTES.md files must be prepared and committed for X.Y.Z first.
EOF
}

if (($# == 1)) && [[ "$1" == "--help" || "$1" == "-h" ]]; then
  usage
  exit 0
fi

if (($# != 1)); then
  usage >&2
  exit 2
fi

version="$1"
if [[ ! "$version" =~ ^[0-9]+\.[0-9]+\.[0-9]+$ ]]; then
  echo "Release version must use X.Y.Z format: $version" >&2
  exit 2
fi

for command in gh git node; do
  if ! command -v "$command" >/dev/null 2>&1; then
    echo "Required command not found: $command" >&2
    exit 1
  fi
done

node scripts/read-release-notes.mjs \
  engine/RELEASE_NOTES.md "MAKO Renderer" "$version" >/dev/null
node scripts/read-release-notes.mjs \
  plugin/RELEASE_NOTES.md "MAKO Decky" "$version" >/dev/null

if command -v sha256sum >/dev/null 2>&1; then
  checksum_command=(sha256sum)
elif command -v shasum >/dev/null 2>&1; then
  checksum_command=(shasum -a 256)
else
  echo "Required command not found: sha256sum or shasum" >&2
  exit 1
fi

current_branch="$(git branch --show-current)"
if [[ "$current_branch" != "main" ]]; then
  echo "Publish from main; current branch is ${current_branch:-detached HEAD}." >&2
  exit 1
fi

if [[ -n "$(git status --porcelain --untracked-files=normal)" ]]; then
  echo "Working tree has uncommitted changes. Commit or stash them before publishing." >&2
  exit 1
fi

if ! git remote get-url origin >/dev/null 2>&1; then
  echo "Publishing requires the origin remote." >&2
  exit 1
fi

if ! gh auth status >/dev/null 2>&1; then
  echo "Publishing requires an authenticated GitHub CLI session. Run: gh auth login -h github.com" >&2
  exit 1
fi

github_repository="$(git remote get-url origin)"
github_repository="${github_repository#git@github.com:}"
github_repository="${github_repository#https://github.com/}"
github_repository="${github_repository%.git}"
if [[ "$github_repository" != */* ]]; then
  echo "Could not determine a GitHub owner/repository from origin." >&2
  exit 1
fi

git fetch origin main --tags --quiet
if [[ "$(git rev-list --count HEAD..origin/main)" != "0" ]]; then
  echo "Local main is behind origin/main. Update it before publishing." >&2
  exit 1
fi

renderer_tag="render-v$version"
decky_tag="plugin-v$version"
renderer_archive="mako-render-v$version-linux.tar.xz"
flatpak_archive="mako-render-v$version-flatpaks.tar.xz"
decky_archive="MAKO-Decky-v$version.zip"

release_has_asset() {
  local tag="$1"
  local expected_asset="$2"
  local assets
  if ! assets="$(gh release view "$tag" --repo "$github_repository" --json assets --jq '.assets[].name' 2>/dev/null)"; then
    return 1
  fi
  while IFS= read -r asset; do
    if [[ "$asset" == "$expected_asset" ]]; then
      return 0
    fi
  done <<< "$assets"
  return 1
}

release_asset_checksum_matches() {
  local tag="$1"
  local asset="$2"
  local expected_checksum="$3"
  local remote_digest
  remote_digest="$(
    gh api "repos/$github_repository/releases/tags/$tag" \
      --jq ".assets[] | select(.name == \"$asset\") | .digest" 2>/dev/null || true
  )"
  if [[ "$remote_digest" == sha256:* ]]; then
    [[ "${remote_digest#sha256:}" == "$expected_checksum" ]]
    return
  fi

  local verification_dir
  local downloaded_checksum
  verification_dir="$(mktemp -d "${TMPDIR:-/tmp}/mako-release-verify.XXXXXX")"
  if ! gh release download "$tag" --repo "$github_repository" \
    --pattern "$asset" --dir "$verification_dir"; then
    rm -rf -- "$verification_dir"
    return 1
  fi
  downloaded_checksum="$("${checksum_command[@]}" "$verification_dir/$asset" | awk '{print $1}')"
  rm -rf -- "$verification_dir"
  [[ "$downloaded_checksum" == "$expected_checksum" ]]
}

renderer_pin_matches() {
  local tag_commit="$1"
  node -e '
    const { resolve } = require("node:path");
    const manifest = require(resolve(process.argv[1]));
    const version = process.argv[2];
    const tagCommit = process.argv[3];
    const repository = process.argv[4];
    const binary = manifest.remote_binary?.[0];
    const tag = `render-v${version}`;
    const nativeName = `mako-render-v${version}-linux.tar.xz`;
    const flatpakName = `mako-render-v${version}-flatpaks.tar.xz`;
    const base = `https://github.com/${repository}/releases/download/${tag}`;
    const sha256 = /^[0-9a-f]{64}$/;
    const valid = binary?.version === version &&
      binary?.release_tag === tag &&
      binary?.source_commit === tagCommit &&
      binary?.name === nativeName &&
      binary?.url === `${base}/${nativeName}` &&
      sha256.test(binary?.sha256hash ?? "") &&
      binary?.flatpak_bundle?.name === flatpakName &&
      binary?.flatpak_bundle?.url === `${base}/${flatpakName}` &&
      sha256.test(binary?.flatpak_bundle?.sha256hash ?? "");
    process.exit(valid ? 0 : 1);
  ' plugin/package.json "$version" "$tag_commit" "$github_repository"
}

renderer_is_complete() {
  local native_checksum
  local flatpak_checksum
  [[ "$(tr -d '[:space:]' < engine/VERSION)" == "$version" ]] || return 1
  git show-ref --verify --quiet "refs/tags/$renderer_tag" || return 1
  gh release view "$renderer_tag" --repo "$github_repository" >/dev/null 2>&1 || return 1
  release_has_asset "$renderer_tag" "$renderer_archive" || return 1
  release_has_asset "$renderer_tag" "$flatpak_archive" || return 1
  renderer_pin_matches "$(git rev-list -n 1 "$renderer_tag")" || return 1
  read -r native_checksum flatpak_checksum < <(
    node -e '
      const { resolve } = require("node:path");
      const binary = require(resolve(process.argv[1])).remote_binary[0];
      process.stdout.write(`${binary.sha256hash}\t${binary.flatpak_bundle.sha256hash}\n`);
    ' plugin/package.json
  )
  release_asset_checksum_matches "$renderer_tag" "$renderer_archive" "$native_checksum" || return 1
  release_asset_checksum_matches "$renderer_tag" "$flatpak_archive" "$flatpak_checksum" || return 1
}

decky_is_complete() {
  local package_version
  package_version="$(node -p 'require("./plugin/package.json").version')"
  [[ "$package_version" == "$version" ]] || return 1
  git show-ref --verify --quiet "refs/tags/$decky_tag" || return 1
  gh release view "$decky_tag" --repo "$github_repository" >/dev/null 2>&1 || return 1
  release_has_asset "$decky_tag" "$decky_archive" || return 1
}

renderer_verified=false
if renderer_is_complete; then
  echo "MAKO Renderer v$version is already complete; skipping its build and upload."
  renderer_verified=true
else
  "$repository_root/engine/scripts/publish-package.sh" --version "$version"
  if renderer_is_complete; then
    renderer_verified=true
  fi
fi

if [[ "$renderer_verified" != true ]]; then
  echo "MAKO Renderer v$version did not pass the final release-state verification." >&2
  exit 1
fi

decky_verified=false
if decky_is_complete; then
  echo "MAKO Decky v$version is already complete; skipping its build and upload."
  decky_verified=true
else
  "$repository_root/plugin/scripts/publish-package.sh" --version "$version"
  if decky_is_complete; then
    decky_verified=true
  fi
fi

if [[ "$decky_verified" != true ]]; then
  echo "MAKO Decky v$version did not pass the final release-state verification." >&2
  exit 1
fi

node scripts/update-release-links.mjs renderer "$version" "$github_repository"
node scripts/update-release-links.mjs decky "$version" "$github_repository"
release_link_readmes=(README.md plugin/README.md engine/README.md)
if ! git diff --quiet -- "${release_link_readmes[@]}"; then
  git add "${release_link_readmes[@]}"
  git commit -m "docs: sync MAKO v$version release links"
fi

latest_tag="$(gh api "repos/$github_repository/releases/latest" --jq '.tag_name')"
if [[ "$latest_tag" != "$decky_tag" ]]; then
  gh release edit "$decky_tag" --repo "$github_repository" --latest
fi
latest_tag="$(gh api "repos/$github_repository/releases/latest" --jq '.tag_name')"
if [[ "$latest_tag" != "$decky_tag" ]]; then
  echo "GitHub Latest points to $latest_tag instead of $decky_tag." >&2
  exit 1
fi

git push origin main

if [[ -n "$(git status --porcelain --untracked-files=normal)" ]]; then
  echo "Release completed, but the worktree is not clean. Review it before continuing." >&2
  git status --short >&2
  exit 1
fi

echo "Published and verified MAKO v$version:"
echo "  Renderer: https://github.com/$github_repository/releases/tag/$renderer_tag"
echo "  Decky:    https://github.com/$github_repository/releases/tag/$decky_tag"
