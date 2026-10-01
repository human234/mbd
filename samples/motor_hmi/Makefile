# SPDX-License-Identifier: Apache-2.0
#
# Build and flash the Motor Control HMI on the one board it runs on.
#
#   make            build
#   make flash      build, then flash and reset
#   make probes     show the bound ST-Link probe
#   make pristine   rebuild from scratch
#   make clean      remove the build dir
#
# Overridable: BOARD BUILD_ROOT PRISTINE BUILD_JOBS PROBE STM32CUBEPROG
#
#   make flash PROBE=002F001C4D4B500B20373831
#   make PRISTINE=always
#
# There is deliberately no G474 target: the panel + touch + LVGL stack needs
# more flash and RAM than the G474RE has (512 KB flash / 128 KB SRAM).

SAMPLE_DIR := $(patsubst %/,%,$(dir $(abspath $(lastword $(MAKEFILE_LIST)))))

BOARD      ?= nucleo_h743zi
BUILD_ROOT ?= $(SAMPLE_DIR)/build
PRISTINE   ?= auto
BUILD_JOBS ?= 8

STM32CUBEPROG ?= STM32_Programmer_CLI

# -i/--dev-id is appended to the programmer's --connect as sn=<SN>. Empty
# falls back to "first probe found" when the board is unplugged or the
# tool is unavailable.
PROBE ?= $(shell $(STM32CUBEPROG) -l 2>/dev/null | sed 's/\x1b\[[0-9;]*m//g' | \
	sed -n '/===== STLink Interface/,/===== UART Interface/p' | \
	awk '/ST-LINK SN *:/{sn=$$NF} /Board Name *:/{if ($$NF == "NUCLEO-H743ZI") {print sn; exit}}')

west-build = west build -p $(PRISTINE) --build-opt=-j$(BUILD_JOBS) -b $(BOARD)

.PHONY: all flash probes pristine clean help

all:
	$(west-build) -d $(BUILD_ROOT) $(SAMPLE_DIR)

flash: all
	west flash -d $(BUILD_ROOT) $(if $(PROBE),--dev-id $(PROBE),) --reset

probes:
	@echo "motor_hmi $(BOARD) probe: $(if $(PROBE),$(PROBE),<not found>)"

pristine:
	$(MAKE) --no-print-directory PRISTINE=always all

clean:
	rm -rf $(BUILD_ROOT)

help:
	@sed -n '3,17s/^# \?//p' $(lastword $(MAKEFILE_LIST))
