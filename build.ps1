<#
.SYNOPSIS
    Build (or flash) the HMI gauge demo inside the self-contained HMI Docker
    image (official Zephyr ci-base container + ARM-only Zephyr SDK + baked
    west workspace at /workdir + STM32CubeProgrammer CLI).

.DESCRIPTION
    No workspace mount is needed: the image already contains the workspace
    snapshot (make stage && make image, or get the prebuilt image). Runs
    `west build` directly against /workdir. Pass -Flash to build+flash
    instead (requires the ST-Link probe to be visible in the container,
    see README-WINDOWS.md).

    Requires: Docker Desktop (WSL2 backend). No Zephyr toolchain or west
    workspace needed on the host.

.EXAMPLE
    PS> .\build.ps1
    PS> .\build.ps1 -Board stm32g474_dk
    PS> .\build.ps1 -Flash
#>
param(
    [string]$Board = "nucleo_g474re",
    [string]$App = "modules/hmi/samples/gauge_demo",
    [switch]$Flash
)

$ErrorActionPreference = "Stop"

$Image = "ghcr.io/human234/hmi-build:0.2.0"

if ($Flash) {
    docker run --rm -it --privileged --workdir /workdir $Image `
        sh -lc "west build -b $Board -p always $App && west flash"
} else {
    docker run --rm -it --workdir /workdir $Image west build -b $Board -p always $App
}