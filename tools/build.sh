#!/usr/bin/env bash
#
# Aethergram build wrapper.
#
# Wraps the upstream tdesktop docker build (Telegram/build/docker/centos_env,
# same invocation as .github/workflows/linux.yml) so that:
#   * API credentials come from a private env file and are passed to cmake
#     as -D flags without ever being printed or committed,
#   * ccache state lives in ~/.cache/aethergram-ccache (owned by you, since
#     the container runs as your uid; a docker volume would be root's),
#   * autoupdate and crash reporting are always disabled.
#
# Usage:
#   tools/build.sh [--test]
#
# Environment:
#   CONFIG                cmake --build config, Debug (default) or Release
#   JOBS                  parallelism (default 2), passed as CMAKE_BUILD_PARALLEL_LEVEL,
#                         default 2 (a single file can need 5 GB of RAM)
#   AETHERGRAM_API_ENV    credentials file, default
#                         ~/.config/aethergram/api.env
#
# Never run this script with xtrace: the credentials would leak to the log.

set -euo pipefail

cd "$(dirname "$0")/.."

env_file="${AETHERGRAM_API_ENV:-$HOME/.config/aethergram/api.env}"

# Real option names, see cmake/variables.cmake, Telegram/CMakeLists.txt and
# the docker build step in .github/workflows/linux.yml.
defines=(
    -D DESKTOP_APP_DISABLE_AUTOUPDATE=ON
    -D DESKTOP_APP_DISABLE_CRASH_REPORTS=ON
)

use_test_api=0
for arg in "$@"; do
    case "$arg" in
        --test) use_test_api=1 ;;
        *) printf 'usage: %s [--test]\n' "$0" >&2; exit 2 ;;
    esac
done

if [ "$use_test_api" -eq 1 ] || [ ! -f "$env_file" ]; then
    if [ "$use_test_api" -eq 0 ]; then
        printf 'aethergram: %s not found, building with the public test API credentials.\n' "$env_file" >&2
    else
        printf 'aethergram: --test given, building with the public test API credentials.\n' >&2
    fi
    defines+=( -D TDESKTOP_API_TEST=ON )
else
    perms="$(stat -c %a "$env_file")"
    if [ "$perms" != 600 ] && [ "$perms" != 400 ]; then
        printf 'aethergram: refusing to read %s: mode is %s, expected 600 (or 400).\n' \
            "$env_file" "$perms" >&2
        printf 'aethergram: chmod 600 %s\n' "$env_file" >&2
        exit 1
    fi

    # Source in a subshell (command substitution) so the env file cannot alter
    # this script's variables, and discard anything it writes to stdout. Each
    # value is captured on its own so that an unset variable cannot be masked
    # by trailing-newline stripping.
    read_creds() {
        . "$env_file" >/dev/null
        printf '%s' "${!1}"
    }
    api_id="$(read_creds TG_API_ID)"
    api_hash="$(read_creds TG_API_HASH)"

    if ! [[ "$api_id" =~ ^[0-9]+$ && "$api_hash" =~ ^[A-Za-z0-9]+$ ]]; then
        printf 'aethergram: %s must define non-empty TG_API_ID (numeric) and TG_API_HASH (alphanumeric).\n' "$env_file" >&2
        exit 1
    fi

    defines+=(
        # Explicit OFF: cmake caches options, so an earlier --test build
        # would otherwise keep using the public test id.
        -D TDESKTOP_API_TEST=OFF
        -D TDESKTOP_API_ID="$api_id"
        -D TDESKTOP_API_HASH="$api_hash"
    )
fi

ccache_dir="${AETHERGRAM_CCACHE_DIR:-$HOME/.cache/aethergram-ccache}"
mkdir -p "$ccache_dir"

exec docker run --rm \
    -u "$(id -u):$(id -g)" \
    -v "$PWD:/usr/src/tdesktop" \
    -v "$ccache_dir:/var/cache/ccache" \
    -e CONFIG="${CONFIG:-Debug}" \
    -e CMAKE_BUILD_PARALLEL_LEVEL="${JOBS:-2}" \
    -e CCACHE_SLOPPINESS=pch_defines,time_macros \
    tdesktop:centos_env \
    env -u CCACHE_DISABLE /usr/src/tdesktop/Telegram/build/docker/centos_env/build.sh \
    "${defines[@]}"
