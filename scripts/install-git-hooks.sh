#!/usr/bin/env bash
set -euo pipefail

repository_root=$(git rev-parse --show-toplevel)
cd "$repository_root"

git config core.hooksPath .githooks
printf '%s\n' 'MAKO Git hooks enabled from .githooks/'
