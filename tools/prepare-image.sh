#!/bin/bash
# Builds the tdesktop:centos_env image that tools/build.sh uses, tamed for a
# 16 GB laptop: no debug/LTO image, every make -j pinned to JOBS (upstream's
# Dockerfile uses $(nproc), which starved the desktop), and expat upgraded so
# the image's python3.12 can import pyexpat (Rocky 8 ships the newer python
# with the old expat 2.2.5, which breaks the appstream changelog step).
set -euo pipefail
cd "$(dirname "$0")/../Telegram/build/docker/centos_env"
jobs="${JOBS:-4}"
dockerfile="$(mktemp)"
trap 'rm -f "$dockerfile"' EXIT
poetry install --no-root >/dev/null
DEBUG= LTO= JOBS="$jobs" poetry run gen_dockerfile > "$dockerfile"
sed -i "s/\$(nproc)/$jobs/g; s/\`nproc\`/$jobs/g" "$dockerfile"
cat >> "$dockerfile" <<'LAYER'

USER root
RUN dnf -y update expat && dnf clean all
USER user
LAYER
DOCKER_BUILDKIT=1 docker build -t tdesktop:centos_env - < "$dockerfile"
