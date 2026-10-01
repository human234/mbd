# syntax=docker/dockerfile:1
#
# SPDX-License-Identifier: Apache-2.0
#
# zccli image: a Zephyr toolchain that hides Zephyr.
#
# Rebases the build environment on a plain Ubuntu 24.04 instead of Zephyr's
# ci-base image (11.6 GB of CI tooling), and bakes in only what building the
# two supported boards needs:
#
#   1. The Zephyr SDK, ARM target toolchain only -- no hosttools sysroot.
#      hosttools/ is 1.2 GB of Yocto; Zephyr merely appends it to
#      CMAKE_PREFIX_PATH (cmake/zephyr/host-tools.cmake), so system
#      cmake/ninja/dtc from apt replace it. That also removes the need for
#      the /home/<user> symlink hack the ci-base build used to work around
#      hosttools' absolute PT_INTERP (the ARM toolchain is unaffected: its
#      PT_INTERP is the normal /lib64/ld-linux-x86-64.so.2).
#
#   2. A pruned west workspace at /workdir (see zccli/stage.sh for exactly
#      what is excluded and why), so `west build` runs fully offline with
#      no -v mount. Re-run zccli/stage.sh after changing sources.
#
#   3. west + Zephyr's Python build requirements in /opt/venv.
#
#   4. /entrypoint.sh -- the command dispatcher zccli.ps1 talks to.
#
# No STM32CubeProgrammer: flashing happens on the Windows host over SWD, so
# the container never needs USB access at all.
#
# Build (uses BuildKit named contexts; Docker >= 23):
#   zccli/stage.sh /tmp/zccli-stage
#   docker build --build-context sdk-src=/tmp/zccli-stage/sdk \
#                --build-context ws-src=/tmp/zccli-stage/ws \
#                -f zccli/Dockerfile -t ghcr.io/human234/zccli:0.1.0 .
#   (or just: make zccli-image)

FROM ubuntu:24.04

ARG DEBIAN_FRONTEND=noninteractive

ENV ZEPHYR_TOOLCHAIN_VARIANT=zephyr \
    ZEPHYR_SDK_INSTALL_DIR=/opt/toolchains/zephyr-sdk-1.0.1 \
    ZSDK_VERSION=1.0.1 \
    ZEPHYR_BASE=/workdir/zephyr \
    HOME=/home/zephyr \
    PATH=/opt/venv/bin:$PATH

USER root

# --- host tools -----------------------------------------------------------
# From Zephyr's own getting-started list (doc/develop/getting_started), minus
# the packages a cross-compile-only image does not use:
#   ccache, dfu-util, wget, xz-utils  -- not used by the build
#   libsdl2-dev                       -- only for native_sim / QEMU SDL
#   *-multilib                        -- only for 32-bit *host* builds
# cmake 3.28.3 (min 3.28.0), dtc 1.7.0 (min 1.4.6), python 3.12.3 (min 3.12)
# are all satisfied by Ubuntu 24.04's own packages.
RUN apt-get update && apt-get install -y --no-install-recommends \
        ca-certificates \
        git \
        cmake \
        ninja-build \
        gperf \
        device-tree-compiler \
        pkg-config \
        make \
        gcc \
        g++ \
        file \
        python3 \
        python3-venv \
        python3-pip \
        python3-dev \
    && rm -rf /var/lib/apt/lists/*

# --- Python build requirements -------------------------------------------
# Zephyr's own "required to build" set (zephyr/scripts/requirements-base.txt),
# installed into a venv so Ubuntu's EXTERNALLY-MANAGED guard is not involved.
# ~47 MB installed.
COPY --from=ws-src zephyr/scripts/requirements-base.txt /tmp/requirements-base.txt
RUN python3 -m venv /opt/venv \
    && /opt/venv/bin/pip install --no-cache-dir --upgrade pip \
    && /opt/venv/bin/pip install --no-cache-dir -r /tmp/requirements-base.txt \
    && rm -f /tmp/requirements-base.txt \
    && /opt/venv/bin/west --version

# --- Zephyr SDK (ARM target toolchain only) -------------------------------
COPY --from=sdk-src gnu/arm-zephyr-eabi /opt/toolchains/zephyr-sdk-1.0.1/gnu/arm-zephyr-eabi
# Multi-source COPY puts a directory source's CONTENTS straight into dest, so
# cmake must be given an explicit subdir destination to mirror the layout the
# SDK's Zephyr-sdkConfigVersion.cmake expects: <sdk>/{cmake/, sdk_version, gnu/}
COPY --from=sdk-src cmake /opt/toolchains/zephyr-sdk-1.0.1/cmake/
COPY --from=sdk-src setup.sh sdk_version sdk_gnu_toolchains /opt/toolchains/zephyr-sdk-1.0.1/

RUN set -eux; \
    chmod +x /opt/toolchains/zephyr-sdk-1.0.1/setup.sh; \
    /opt/toolchains/zephyr-sdk-1.0.1/gnu/arm-zephyr-eabi/bin/arm-zephyr-eabi-gcc --version; \
    /opt/toolchains/zephyr-sdk-1.0.1/gnu/arm-zephyr-eabi/bin/arm-zephyr-eabi-size --version

# --- pruned west workspace ------------------------------------------------
COPY --from=ws-src . /workdir/

# --- entrypoint -----------------------------------------------------------
COPY zccli/entrypoint.sh /entrypoint.sh
RUN chmod +x /entrypoint.sh

# Runtime layout. /src is normally a bind mount of the user's project folder
# and /build a named volume (the build cache) -- both are created here so the
# image also works when run without either.
RUN mkdir -p /src /build /home/zephyr \
    && chown -R 1000:1000 /workdir /src /build /home/zephyr

USER 1000:1000
WORKDIR /src

# `docker run -it IMAGE build main.c` works directly; zccli.ps1 overrides this
# with --entrypoint sleep so the container stays up between `docker exec`s.
ENTRYPOINT ["/entrypoint.sh"]
CMD ["help"]
