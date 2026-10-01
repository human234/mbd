# zccli image build wrappers.
#
# zccli is the end-user toolchain image (Ubuntu base, pruned workspace, no
# STM32CubeProgrammer -- flashing happens on the Windows host). See zccli/.
# After sources change, re-stage the workspace and rebuild the image:
#   make zccli-stage
#   make zccli-image

REPO_DIR := $(dir $(realpath $(lastword $(MAKEFILE_LIST))))

# zccli: the end-user toolchain image. See zccli/.
ZCCLI_IMAGE ?= ghcr.io/human234/zccli:$(shell cat "$(REPO_DIR)zccli/VERSION" 2>/dev/null || echo 0.1.0)

# Must match the default in zccli/stage.sh and zccli/docker-build.sh so that
# `make zccli-stage` and `make zccli-image` share one staging directory.
STAGE_DIR  ?= $(if $(TMPDIR),$(TMPDIR),/tmp)/zccli-stage

.PHONY: help zccli-stage zccli-image zccli-push

help:
	@echo "make zccli-stage - stage workspace+SDK pruned for the zccli image"
	@echo "make zccli-image - build $(ZCCLI_IMAGE)"
	@echo "make zccli-push  - build + push $(ZCCLI_IMAGE)"

zccli-stage:
	"$(REPO_DIR)zccli/stage.sh" "$(STAGE_DIR)"

zccli-image:
	IMAGE="$(ZCCLI_IMAGE)" STAGE_DIR="$(STAGE_DIR)" "$(REPO_DIR)zccli/docker-build.sh"

zccli-push:
	IMAGE="$(ZCCLI_IMAGE)" STAGE_DIR="$(STAGE_DIR)" PUSH=1 "$(REPO_DIR)zccli/docker-build.sh"
