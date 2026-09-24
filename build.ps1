<#
.SYNOPSIS
    Build the HMI gauge demo inside the persistent `hmi-build` container.

.DESCRIPTION
    Uses the self-contained HMI image ghcr.io/human234/hmi-build:0.2.0
    (Zephyr SDK + baked west workspace at /workdir + STM32CubeProgrammer).
    A long-running, privileged container named `hmi-build` is (re)created on
    first use and `west build` runs inside it via `docker exec`. The build dir
    lives in the container, so repeat builds are incremental.

    To flash, the ST-Link probe must first be passed into the WSL2 VM
    (usbipd-win, see README-WINDOWS.md).

.EXAMPLE
    PS> .\build.ps1
    PS> .\build.ps1 -Pristine
#>
[CmdletBinding()]
param(
    [string]$Board = "nucleo_g474re",
    [string]$App = "samples/gauge_demo",
    [switch]$Pristine
)

$ErrorActionPreference = "Stop"

$Image   = "ghcr.io/human234/hmi-build:0.2.0"
$CtrName = "hmi-build"
$WorkDir = "/workdir/apps/HMI"

# Ensure the persistent build container exists and is running (idempotent).
$ctrId = docker ps -aq --filter "name=^/$CtrName$"
if (-not $ctrId) {
    docker run -d --rm --privileged --name $CtrName -w $WorkDir $Image sleep infinity
} elseif ((docker inspect --format '{{.State.Running}}' $CtrName) -ne "true") {
    docker start $CtrName
}

if ($Pristine) {
    docker exec -w $WorkDir $CtrName west build -b $Board -p always $App
} else {
    docker exec -w $WorkDir $CtrName west build -b $Board $App
}