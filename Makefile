# HMI Docker build wrappers.
#
# Runs west commands inside the self-contained HMI image (official Zephyr
# ci-base container + ARM-only Zephyr SDK + baked workspace + STM32CubeProgrammer
# CLI). The image contains a snapshot of the west workspace at /workdir, so no
# `-v` mount is needed and builds work identically on Linux, macOS, and Windows.
#
# After sources change, re-bake the workspace and rebuild the image:
#   make stage
#   make image

IMAGE    ?= ghcr.io/human234/hmi-build:0.2.0
BOARD    ?= nucleo_g474re
APP      ?= modules/hmi/samples/gauge_demo
STAGE_DIR?= /tmp/hmi-stage
REPO_DIR := $(dir $(realpath $(lastword $(MAKEFILE_LIST))))

.PHONY: help stage image build flash shell

help:
	@echo "make stage   - stage .west/zephyr/SDK into $(STAGE_DIR)"
	@echo "make image   - rebuild the HMI Docker image ($(IMAGE))"
	@echo "make build   - build the HMI gauge demo ($(BOARD)) offline in Docker"
	@echo "make flash   - build + flash the demo to the board (needs USB access)"
	@echo "make shell   - open an interactive shell in the HMI Docker image"

stage:
	"$(REPO_DIR)docker/rebake-workspace.sh" "$(STAGE_DIR)"

image: stage
	docker build --build-context sdk-src="$(STAGE_DIR)/sdk" --build-context ws-src="$(STAGE_DIR)/ws" \
		-f "$(REPO_DIR)docker/Dockerfile" -t $(IMAGE) "$(REPO_DIR)"

build:
	docker run --rm -it --workdir /workdir $(IMAGE) west build -b $(BOARD) -p always $(APP)

# Flashing from a container needs access to the debug probe USB device.
# On Windows/WSL2 first bind the probe into WSL2 (usbipd-win) or use
# Docker Desktop's USB passthrough; --privileged is used so the ST-Link
# is reachable regardless of how it was attached.
flash:
	docker run --rm -it --privileged --workdir /workdir $(IMAGE) \
		sh -lc 'west build -b $(BOARD) -p always $(APP) && west flash'

shell:
	docker run --rm -it --workdir /workdir $(IMAGE) bash