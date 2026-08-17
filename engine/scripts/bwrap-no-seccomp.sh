#!/usr/bin/env bash
# Docker Desktop fallback for local Flatpak packaging only.
# Flatpak passes --seccomp <fd> to Bubblewrap. Docker Desktop kernels that do
# not expose CONFIG_SECCOMP_FILTER cannot accept that nested filter, so remove
# precisely that option. The enclosing Docker build container is already local,
# privileged, and used only for this trusted source build.
set -euo pipefail

arguments=()
while (($#)); do
    if [[ "$1" == "--seccomp" ]]; then
        shift 2
        continue
    fi
    if [[ "$1" == "--args" ]]; then
        argument_fd="$2"
        expanded_arguments=()
        # Flatpak encodes its Bubblewrap arguments as NUL-separated values in
        # this inherited descriptor. Expand them so the nested --seccomp pair
        # can be removed below before invoking the real Bubblewrap binary.
        readarray -d '' expanded_arguments < "/proc/self/fd/$argument_fd"
        shift 2
        set -- "${expanded_arguments[@]}" "$@"
        continue
    fi
    arguments+=("$1")
    shift
done

exec /usr/bin/bwrap "${arguments[@]}"
