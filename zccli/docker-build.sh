#!/usr/bin/env bash
#
# SPDX-License-Identifier: Apache-2.0
#
# Stage + build (+ optionally push) the zccli image.
#
#   zccli/docker-build.sh                 build as ghcr.io/human234/zccli:<tag>
#   IMAGE=my/zccli:dev zccli/docker-build.sh
#   PUSH=1 zccli/docker-build.sh          also push to ghcr.io
#
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(dirname "$SCRIPT_DIR")"

VERSION="$(cat "$REPO_ROOT/zccli/VERSION" 2>/dev/null || echo 0.1.0)"
IMAGE="${IMAGE:-ghcr.io/human234/zccli:$VERSION}"
STAGE_DIR="${STAGE_DIR:-${TMPDIR:-/tmp}/zccli-stage}"

echo "image  : $IMAGE"
echo "stage  : $STAGE_DIR"
echo

"$SCRIPT_DIR/stage.sh" "$STAGE_DIR"

echo
echo "== building image =="
docker build \
	--build-context sdk-src="$STAGE_DIR/sdk" \
	--build-context ws-src="$STAGE_DIR/ws" \
	-f "$SCRIPT_DIR/Dockerfile" \
	-t "$IMAGE" \
	"$REPO_ROOT"

echo
echo "== image size =="
docker image inspect "$IMAGE" --format '{{.Size}} bytes' | awk '{printf "  %.2f GB\n", $1/1024/1024/1024}'
docker images --format '  {{.Repository}}:{{.Tag}}  {{.Size}}' | grep -F "${IMAGE#*:}" || true

if [ "${PUSH:-0}" = "1" ]; then
	echo
	echo "== pushing $IMAGE =="
	docker push "$IMAGE"
fi

echo
echo "done: $IMAGE"
