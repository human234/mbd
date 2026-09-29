# SPDX-License-Identifier: Apache-2.0
#
# Build (and flash) both svpwm_comm roles from one command.
#
# The two roles are two independent CMake projects on two boards, and
# `west build` takes a single -b/-d pair per invocation -- there is no
# multi-board flag. So the fan-out lives here: one build dir per role,
# built in parallel.
#
# The role itself is NOT passed on the command line: Zephyr merges
# boards/<board>.conf and boards/<board>.overlay from the app dir
# (cmake/modules/configuration_files.cmake), and those files carry
# CONFIG_SVPWM_COMM_ROLE_HOST / _DEVICE plus the display/touch setup.
#
#   make              build both roles in parallel
#   make host         build only the host   (NUCLEO-G474RE)
#   make device       build only the device (NUCLEO-H743ZI)
#   make flash        flash both boards and reset them into the new image
#   make probes       show which ST-Link probe is bound to which role
#   make pristine     rebuild both from scratch
#   make clean        remove both build dirs
#
# Overridable: HOST_BOARD DEVICE_BOARD BUILD_ROOT PRISTINE BUILD_JOBS
#              HOST_PROBE DEVICE_PROBE STM32CUBEPROG
#
#   make device PRISTINE=always         clean rebuild of the device role
#   make flash HOST_PROBE=0033...       flash a specific probe

SAMPLE_DIR := $(patsubst %/,%,$(dir $(abspath $(lastword $(MAKEFILE_LIST)))))

HOST_BOARD   ?= nucleo_g474re
DEVICE_BOARD ?= nucleo_h743zi
BUILD_ROOT   ?= $(SAMPLE_DIR)/build

# -p auto is the west default; `make PRISTINE=always` for a clean rebuild.
PRISTINE     ?= auto
# Two roles build concurrently, so halve the default ninja job count
# (8 cores here) instead of letting both builds fight over them.
BUILD_JOBS   ?= 4

# ST-Link probe selection. Both boards flash through the same runner
# (stm32cubeprogrammer --port=swd), so with two probes plugged in the
# right one has to be named explicitly: -i/--dev-id is appended to the
# programmer's --connect as sn=<SN>. Default to the probe whose board
# name matches the role, read back from the programmer; empty (fall back
# to "first probe found") when the board is not plugged in, or when
# STM32_Programmer_CLI is unavailable.
STM32CUBEPROG ?= STM32_Programmer_CLI

probe-sn = $(shell $(STM32CUBEPROG) -l 2>/dev/null | sed 's/\x1b\[[0-9;]*m//g' | \
	sed -n '/===== STLink Interface/,/===== UART Interface/p' | \
	awk -v want=$(1) '/ST-LINK SN *:/{sn=$$NF} /Board Name *:/{if ($$NF == want) {print sn; exit}}')

HOST_PROBE   ?= $(call probe-sn,NUCLEO-G474RE)
DEVICE_PROBE ?= $(call probe-sn,NUCLEO-H743ZI)

# --build-opt is for the build tool (ninja), not for cmake. The "=" form is
# required: `-o -j4` would be parsed by west as a missing argument.
west-build = west build -p $(PRISTINE) --build-opt=-j$(BUILD_JOBS)

.PHONY: all host device flash flash-host flash-device probes pristine clean help

all:
	@$(MAKE) --no-print-directory -j2 host device

host:
	$(west-build) -b $(HOST_BOARD) -d $(BUILD_ROOT)/host $(SAMPLE_DIR)

device:
	$(west-build) -b $(DEVICE_BOARD) -d $(BUILD_ROOT)/device $(SAMPLE_DIR)

flash: flash-host flash-device

flash-host:
	west flash -d $(BUILD_ROOT)/host $(if $(HOST_PROBE),--dev-id $(HOST_PROBE),) --reset

flash-device:
	west flash -d $(BUILD_ROOT)/device $(if $(DEVICE_PROBE),--dev-id $(DEVICE_PROBE),) --reset

probes:
	@echo "host   $(HOST_BOARD)   probe: $(if $(HOST_PROBE),$(HOST_PROBE),<not found>)"
	@echo "device $(DEVICE_BOARD) probe: $(if $(DEVICE_PROBE),$(DEVICE_PROBE),<not found>)"

pristine:
	@$(MAKE) --no-print-directory PRISTINE=always all

clean:
	rm -rf $(BUILD_ROOT)

help:
	@sed -n '3,30s/^# \?//p' $(lastword $(MAKEFILE_LIST))
