# syntax=docker/dockerfile:1

# Self-contained HMI build / flash image based on the official Zephyr *CI base*
# image (ci-base), which has all the CI tools + west Python venv but NO Zephyr
# SDK. On top of that we bake in:
#
#   1. The Zephyr SDK (v1.0.1, matching zephyr/SDK_VERSION for Zephyr 4.4.x),
#      restricted to the ARM toolchain subset actually needed for the
#      nucleo_g474re target: gnu/arm-zephyr-eabi + hosttools + cmake config,
#      staged from the local SDK install by docker/rebake-workspace.sh. This
#      drops ~12 GB of unused architecture toolchains / LLVM vs. the full SDK.
#
#   2. A snapshot of the west workspace (docker/rebake-workspace.sh) at
#      /workdir: zephyr + hal/stm32 + cmsis_6 + lvgl + picolibc + the HMI
#      module. The image therefore runs `west build` fully offline with no
#      `-v` mount; re-run "make stage && make image" after changing sources.
#
#   3. The STM32CubeProgrammer CLI (docker/stm32cubeprog-tree.tar.gz) so
#      `west flash` works in-container; ci-base/zephyr-build do not ship it.
#
# Build (uses BuildKit named contexts; Docker >= 23):
#   docker build --build-context sdk-src=$STAGE/sdk --build-context ws-src=$STAGE/ws \
#     -f docker/Dockerfile -t ghcr.io/human234/hmi-build:0.2.0 .
#   (or just: make image)

FROM ghcr.io/zephyrproject-rtos/ci-base:v0.29.3

ARG CUBEPROG_TAR=docker/stm32cubeprog-tree.tar.gz

ENV ZEPHYR_TOOLCHAIN_VARIANT=zephyr \
    ZEPHYR_SDK_INSTALL_DIR=/opt/toolchains/zephyr-sdk-1.0.1 \
    ZSDK_VERSION=1.0.1

USER root

# --- 1) Zephyr SDK (ARM-only subset + hosttools) ---
COPY --from=sdk-src gnu/arm-zephyr-eabi /opt/toolchains/zephyr-sdk-1.0.1/gnu/arm-zephyr-eabi
COPY --from=sdk-src hosttools /opt/toolchains/zephyr-sdk-1.0.1/hosttools
# Multi-source COPY puts a directory source's CONTENTS straight into dest
# (no basename subdir), which used to flatten cmake/* onto the SDK root and
# break Zephyr-sdkConfigVersion.cmake's `../sdk_version` lookup. Copy `cmake`
# to an explicit subdir destination instead; mirror the host SDK layout:
#   <sdk>/{cmake/Zephyr-sdkConfig*.cmake, sdk_version, gnu/, hosttools, ...}
COPY --from=sdk-src setup.sh sdk_version sdk_gnu_toolchains /opt/toolchains/zephyr-sdk-1.0.1/
COPY --from=sdk-src cmake /opt/toolchains/zephyr-sdk-1.0.1/cmake/
RUN set -eux; \
    chmod +x /opt/toolchains/zephyr-sdk-1.0.1/setup.sh; \
    # The host SDK's setup.sh patched every ELF's PT_INTERP to the ABSOLUTE
    # host path /home/peter996/zephyr-sdk-1.0.1/.../ld-linux-x86-64.so.2, so
    # those binaries cannot exec in a container. Mirror that prefix with a
    # symlink into the real SDK so the absolute interpreters resolve (this
    # also covers dtc under hosttools/sysroots and the arm gcc chain). \
    mkdir -p /home/peter996; \
    ln -s /opt/toolchains/zephyr-sdk-1.0.1 /home/peter996/zephyr-sdk-1.0.1; \
    /opt/toolchains/zephyr-sdk-1.0.1/gnu/arm-zephyr-eabi/bin/arm-zephyr-eabi-gcc --version

# --- 2) Baked west workspace (offline builds, no -v mount) ---
COPY --from=ws-src . /workdir/
RUN mkdir -p /workdir && chown -R 1000:1000 /workdir

# --- 3) STM32CubeProgrammer CLI (so `west flash` works) ---
COPY ${CUBEPROG_TAR} /tmp/stm32cubeprog-tree.tar.gz
RUN set -eux; \
    mkdir -p /usr/local/STMicroelectronics/STM32Cube; \
    tar -xzf /tmp/stm32cubeprog-tree.tar.gz -C /usr/local/STMicroelectronics/STM32Cube; \
    rm -f /tmp/stm32cubeprog-tree.tar.gz; \
    ln -sf /usr/local/STMicroelectronics/STM32Cube/STM32CubeProgrammer/bin/STM32_Programmer_CLI /usr/local/bin/STM32_Programmer_CLI; \
    LD_LIBRARY_PATH=/usr/local/STMicroelectronics/STM32Cube/STM32CubeProgrammer/lib \
        STM32_Programmer_CLI --version

USER 1000:1000
WORKDIR /workdir