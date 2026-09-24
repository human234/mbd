<#
.SYNOPSIS
    Flash the last build to the board via the persistent `hmi-build` container.

.DESCRIPTION
    Runs `west flash` as root inside the `hmi-build` container (created by
    build.ps1 if missing) so STM32CubeProgrammer can open the passed-through
    ST-Link USB probe. Requires usbipd-win to have attached the probe into the
    WSL2 VM first (see README-WINDOWS.md).

.EXAMPLE
    PS> .\flash.ps1
#>
[CmdletBinding()]
param(
    [string]$Board = "nucleo_g474re"
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

docker exec -w $WorkDir --user root $CtrName west flash