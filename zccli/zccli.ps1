<#
.SYNOPSIS
    zccli -- build and flash an HMI screen without touching Zephyr.

.DESCRIPTION
    Everything Zephyr-specific happens inside a Docker container. This script
    is just the Windows side: it makes sure the container exists, hands
    commands to it, and flashes the finished firmware using STM32CubeProgrammer
    on the host (so the ST-Link never has to be passed into Docker).

    One-time setup:
        .\zccli.ps1 setup

    Day to day:
        .\zccli.ps1 build main.c
        .\zccli.ps1 flash

    Or flash one of the bundled demo apps instead of your own file:
        .\zccli.ps1 sample            (lists them)
        .\zccli.ps1 sample display_demo

    Your .c file should define:

        #include <lvgl.h>
        #include <hmi/hmi.h>
        #include "zccli.h"

        void zc_user_init(lv_obj_t *root) { /* draw your screen */ }

    Everything else (CMake, Kconfig, devicetree, board selection, the RTOS
    startup and the LVGL main loop) is already written for you.

.NOTES
    The project folder you run this from is mounted into the container at
    /src, so pass paths relative to it.
#>
[CmdletBinding()]
param(
    [Parameter(Position = 0)]
    [string]$Command = 'help',

    [Parameter(Position = 1, ValueFromRemainingArguments = $true)]
    [string[]]$Rest = @()
)

# Windows PowerShell 5.1 is the supported runtime, and it has a trap that
# makes 'Stop' actively harmful here: when a *native* command's stderr is
# redirected (`2>$null`), the output still arrives as an ErrorRecord, and
# 'Stop' promotes it to a terminating error. Every docker probe below
# redirects stderr to keep probe noise off the console, so under 'Stop'
# `docker image inspect` reporting "No such image" aborted the whole script
# before $LASTEXITCODE could be read.
#
# Cmdlet failures that do matter are handled explicitly at their call sites
# (try/catch or -ErrorAction), which does not depend on this preference.
$ErrorActionPreference = 'Continue'

$Image    = $env:ZC_IMAGE
if (-not $Image) { $Image = 'ghcr.io/human234/zccli:0.2.0' }
$CtrName  = 'zccli'
$VolName  = 'zccli-build'
$SrcDir   = $PWD.Path          # mounted into the container at /src
$WorkDir  = '/workdir'

function Fail([string]$Msg) {
    Write-Host "zccli: $Msg" -ForegroundColor Red
    exit 1
}

function Have([string]$Exe) {
    return [bool](Get-Command $Exe -ErrorAction SilentlyContinue)
}

# --------------------------------------------------------------- container

function ContainerId() {
    return (docker ps -aq --filter "name=^/$CtrName$" 2>$null)
}

function ContainerRunning() {
    if (-not (ContainerId)) { return $false }
    $state = docker inspect --format '{{.State.Running}}' $CtrName 2>$null
    return ($state -eq 'true')
}

# Returns $true when the container exists but was created for a different
# folder than the one we are being run from.
#
# The answer comes from a label recorded at `docker run` time rather than
# from the mount list, for two independent reasons:
#
#   * The template must contain no string literal. Windows PowerShell 5.1
#     wraps a native argument containing spaces in quotes WITHOUT escaping
#     the quotes already inside it, so a template such as
#         {{if eq .Destination "/src"}}
#     reaches docker as `eq .Destination /src` and Go rejects it with
#         template parsing error: unexpected "/" in operand
#     `{{json ...}}` has no quotes for 5.1 to mangle.
#
#   * The two paths are not directly comparable in any case. Docker Desktop
#     does not necessarily report a mount's Source in the form it was given
#     (a Windows path can come back as /run/desktop/mnt/host/c/...), and
#     [System.Uri] maps any Unix-style rooted path to an EMPTY LocalPath --
#     so the previous comparison silently either always matched or never
#     did, depending on which form the daemon returned. A label is verbatim
#     the string we passed in and needs no translation.
#
# A container created before labels existed has no such label and is treated
# as a match, so an already-working setup is never torn down behind the
# user's back.
function MountMismatch() {
    $raw = docker inspect --format '{{json .Config.Labels}}' $CtrName 2>$null
    if (-not $raw) { return $false }

    try { $labels = ($raw | Out-String) | ConvertFrom-Json } catch { return $false }
    if (-not $labels) { return $false }

    $src = $labels.'zccli.src'
    if (-not $src) { return $false }

    # -ne on strings is case-insensitive, which is what we want on Windows.
    return ($src -ne $SrcDir)
}

# Create or re-point the container. The named volume holding the build cache
# is deliberately untouched, so re-pointing at a new folder keeps the cache.
function Ensure-Container {
    if (-not (ContainerId)) {
        $haveImg = $false
        $null = docker image inspect $Image 2>$null
        if ($LASTEXITCODE -eq 0) { $haveImg = $true }

        if (-not $haveImg) {
            Write-Host "zccli: pulling $Image" -ForegroundColor Cyan
            Write-Host "zccli: first run only -- this is several GB, give it a few minutes." -ForegroundColor DarkGray
            docker pull $Image
            if ($LASTEXITCODE -ne 0) {
                Fail ("could not pull $Image`n" +
                      '  If the repository is private: docker login ghcr.io')
            }
        }

        Write-Host "zccli: starting container ($Image)" -ForegroundColor Cyan
        docker run -d --name $CtrName `
            --label "zccli.src=$SrcDir" `
            -v "${VolName}:/build" `
            -v "${SrcDir}:/src" `
            --entrypoint sleep $Image infinity | Out-Null
        if ($LASTEXITCODE -ne 0) { Fail "could not start the container (is Docker Desktop running?)" }
        return
    }

    if (MountMismatch) {
        Write-Host "zccli: re-pointing container at $SrcDir (build cache kept)" -ForegroundColor Cyan
        docker rm -f $CtrName | Out-Null
        # $? is not trustworthy after `| Out-Null`; $LASTEXITCODE is.
        if ($LASTEXITCODE -ne 0) { Fail "could not remove the old container" }
        docker run -d --name $CtrName `
            --label "zccli.src=$SrcDir" `
            -v "${VolName}:/build" `
            -v "${SrcDir}:/src" `
            --entrypoint sleep $Image infinity | Out-Null
        if ($LASTEXITCODE -ne 0) { Fail "could not start the container" }
        return
    }

    if (-not (ContainerRunning)) {
        docker start $CtrName | Out-Null
        if ($LASTEXITCODE -ne 0) { Fail "could not start the container" }
    }
}

# Forward a command to the entrypoint inside the container.
function Invoke-Entry([string[]]$CmdArgs) {
    Ensure-Container

    # -t needs a real console; drop it when output is redirected.
    $tty = -not ([Console]::IsInputRedirected -or [Console]::IsOutputRedirected)

    # Arguments are handed to docker untouched. An earlier version pre-quoted
    # anything containing whitespace, which is the wrong correction for
    # Windows PowerShell 5.1: 5.1 already wraps a whitespace-bearing argument
    # in quotes, but does not escape quotes already inside it -- so adding
    # our own produced a doubly-quoted command line and docker received the
    # literal quote characters as part of the filename. Letting 5.1 do its
    # own wrapping is correct as long as we stay out of its way.
    if ($tty) {
        docker exec -it -w /src $CtrName /entrypoint.sh @CmdArgs
    } else {
        docker exec -i -w /src $CtrName /entrypoint.sh @CmdArgs
    }
    exit $LASTEXITCODE
}

# ------------------------------------------------------------------ flash

function Find-CubeProgrammer() {
    # 1. Explicit override always wins.
    if ($env:STM32CUBE_PROG) {
        if (Test-Path $env:STM32CUBE_PROG) { return $env:STM32CUBE_PROG }
        Fail "STM32CUBE_PROG points at $env:STM32CUBE_PROG, which does not exist"
    }

    # 2. On PATH.
    $cmd = Get-Command 'STM32_Programmer_CLI.exe' -ErrorAction SilentlyContinue
    if ($cmd) { return $cmd.Source }

    # 3. The default install locations. Built defensively: these env vars can
    #    be unset (PowerShell 7 on some hosts), and Join-Path rejects null.
    $candidates = @()
    foreach ($root in @($env:ProgramFiles, ${env:ProgramFiles(x86)}, $env:LOCALAPPDATA)) {
        if ($root) {
            $candidates += (Join-Path $root 'STMicroelectronics\STM32Cube\STM32CubeProgrammer\bin\STM32_Programmer_CLI.exe')
        }
    }
    foreach ($c in $candidates) {
        if ($c -and (Test-Path $c)) { return $c }
    }
    return $null
}

function Invoke-Flash([string[]]$Args) {
    # Which firmware?
    $hex = Join-Path $SrcDir 'out\zephyr.hex'
    foreach ($a in $Args) {
        if ($a -notlike '--*') { $hex = $a }
    }
    if (-not (Test-Path $hex)) {
        Fail "no firmware at $hex -- build it first: .\zccli.ps1 build main.c"
    }

    $prog = Find-CubeProgrammer
    if (-not $prog) {
        Write-Host ''
        Write-Host 'zccli: STM32CubeProgrammer was not found on this machine.' -ForegroundColor Red
        Write-Host ''
        Write-Host '  Download STM32CubeProgrammer (free) for Windows from:'
        Write-Host '    https://www.st.com/en/development-tools/stm32cubeprog.html'
        Write-Host ''
        Write-Host '  Install it, then either add its bin\ folder to PATH or set:'
        Write-Host '    $env:STM32CUBE_PROG = "C:\Program Files\STMicroelectronics\STM32Cube\STM32CubeProgrammer\bin\STM32_Programmer_CLI.exe"'
        Write-Host ''
        exit 1
    }

    Write-Host "zccli: flashing $hex" -ForegroundColor Cyan
    Write-Host "zccli: using $prog"
    Write-Host ''

    & $prog -c port=SWD -w $hex -rst
    if ($LASTEXITCODE -ne 0) {
        Write-Host ''
        Write-Host 'zccli: flash failed. Checklist:' -ForegroundColor Red
        Write-Host '  1. Board USB cable is plugged in (ST-LINK VCP port)'
        Write-Host '  2. It shows up in Device Manager as "STMicroelectronics STLink"'
        Write-Host '  3. No other program (another IDE, terminal) is holding the probe'
        exit 1
    }
    Write-Host ''
    Write-Host 'zccli: flashed OK -- the board has been reset and is running' -ForegroundColor Green
}

# ----------------------------------------------------------------- doctor

function Invoke-Doctor() {
    $fail = 0
    Write-Host ''
    Write-Host 'zccli doctor -- on this machine' -ForegroundColor Cyan
    # Recorded because 5.1 and 7 differ in how they pass arguments to native
    # commands and in how they treat native stderr -- which is exactly the
    # class of problem doctor exists to surface.
    Write-Host "  info  Windows PowerShell $($PSVersionTable.PSVersion)"

    if (Have 'docker') {
        Write-Host '  ok    docker is installed'
    } else {
        Write-Host '  FAIL  docker is not installed' -ForegroundColor Red
        Write-Host '        Install Docker Desktop: https://docs.docker.com/desktop/setup/install/windows-install/'
        exit 1
    }

    $daemonUp = $false
    try { docker info 2>$null | Out-Null; $daemonUp = ($LASTEXITCODE -eq 0) } catch {}
    if ($daemonUp) {
        Write-Host '  ok    docker daemon is running'
    } else {
        Write-Host '  FAIL  docker daemon is not running -- start Docker Desktop' -ForegroundColor Red
        exit 1
    }

    $img = docker image inspect $Image 2>$null | Out-Null
    if ($LASTEXITCODE -eq 0) {
        Write-Host "  ok    image present ($Image)"
    } else {
        Write-Host "  warn  image not pulled yet -- it downloads on first build" -ForegroundColor Yellow
    }

    $prog = Find-CubeProgrammer
    if ($prog) {
        Write-Host "  ok    STM32CubeProgrammer at $prog"
    } else {
        Write-Host '  warn  STM32CubeProgrammer not found -- "zccli flash" will guide you' -ForegroundColor Yellow
    }

    # The Windows share has to include the project folder or docker run fails.
    Write-Host "  ok    project folder $SrcDir"

    # Probe visibility. Flashing goes over SWD, but an unrecognised board is
    # the most common first-run surprise, so report it if we can see it.
    $probe = @()
    try {
        $probe = @(Get-PnpDevice -ErrorAction SilentlyContinue |
            Where-Object { $_.Status -eq 'OK' -and $_.FriendlyName -match 'ST.?Link' })
    } catch { }
    if ($probe.Count -gt 0) {
        Write-Host "  ok    debug probe detected: $($probe[0].FriendlyName)"
    } else {
        Write-Host '  warn  no ST-LINK probe detected (plug in the board to flash)' -ForegroundColor Yellow
    }

    Write-Host ''
    $null = docker info 2>$null
    if ($LASTEXITCODE -eq 0) {
        Invoke-Entry @('doctor')
    }
}

# ------------------------------------------------------------------- main

switch ($Command.ToLower()) {

    'setup' {
        if (-not (Have 'docker')) { Fail 'docker is not installed' }
        # `docker run` pulls on demand, so this is all setup really is.
        Ensure-Container
        Write-Host "zccli: ready." -ForegroundColor Green
        Write-Host ''
        Write-Host '  .\zccli.ps1 build my_screen.c'
        Write-Host '  .\zccli.ps1 sample display_demo'
        Write-Host '  .\zccli.ps1 flash'
        exit 0
    }

    'update' {
        if (-not (Have 'docker')) { Fail 'docker is not installed' }
        Write-Host "zccli: pulling $Image" -ForegroundColor Cyan
        docker pull $Image
        if ($LASTEXITCODE -ne 0) { Fail 'pull failed (try: docker login ghcr.io)' }
        if (ContainerId) {
            docker rm -f $CtrName | Out-Null
            Write-Host 'zccli: recreated the container (build cache kept)' -ForegroundColor Cyan
        }
        Ensure-Container
        Write-Host 'zccli: updated.' -ForegroundColor Green
        exit 0
    }

    'flash' { Invoke-Flash $Rest }
    'doctor' { Invoke-Doctor }

    'help' {
        # Print the file's own .SYNOPSIS block so header and help cannot drift.
        $raw = Get-Content -Raw -LiteralPath $MyInvocation.MyCommand.Path
        if ($raw -match '(?s)<#(.*?)#>') {
            $doc = $Matches[1]
            $Matches = @{}
            ($doc -split "`r?`n") |
                ForEach-Object { $_ -replace '^#\s{0,1}', '' } |
                Where-Object { $_ -ne '' -and $_ -notmatch '^\.SYN' -and $_ -notmatch '^\.DESCRIPT' -and $_ -notmatch '^\.NOTES' } |
                ForEach-Object { Write-Host $_ }
        }
        Write-Host ''
        Write-Host 'Commands:' -ForegroundColor Cyan
        Write-Host '  setup                    one-time: start the container'
        Write-Host '  build <file.c> [...]     build your screen   [--board <b>] [--clean]'
        Write-Host '  sample [name]            build a bundled demo app (no name = list)'
        Write-Host '  flash [path\to.hex]      write the firmware over SWD'
        Write-Host '  export                   copy firmware to .\out\'
        Write-Host '  size                     show flash/RAM usage'
        Write-Host '  clean                    drop the cached build'
        Write-Host '  doctor                   check everything end to end'
        Write-Host '  update                   pull the latest image'
        Write-Host '  shell                    open a shell in the container'
        exit 0
    }

    default { Invoke-Entry (@($Command) + $Rest) }
}
