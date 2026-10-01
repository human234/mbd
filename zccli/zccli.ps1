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

$ErrorActionPreference = 'Stop'

$Image    = $env:ZC_IMAGE
if (-not $Image) { $Image = 'ghcr.io/human234/zccli:0.1.0' }
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

# Returns $true when the container exists but is mounted on a different
# folder than the one we are being run from.
function MountMismatch() {
    $src = docker inspect --format '{{range .Mounts}}{{if eq .Destination "/src"}}{{.Source}}{{end}}{{end}}' $CtrName 2>$null
    if (-not $src) { return $false }
    try {
        return (([System.Uri]$src).LocalPath -ne ([System.Uri]$SrcDir).LocalPath)
    } catch {
        return ($src -ne $SrcDir)
    }
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
            -v "${VolName}:/build" `
            -v "${SrcDir}:/src" `
            --entrypoint sleep $Image infinity | Out-Null
        if ($LASTEXITCODE -ne 0) { Fail "could not start the container (is Docker Desktop running?)" }
        return
    }

    if (MountMismatch) {
        Write-Host "zccli: re-pointing container at $SrcDir (build cache kept)" -ForegroundColor Cyan
        docker rm -f $CtrName | Out-Null
        if (-not $?) { Fail "could not remove the old container" }
        docker run -d --name $CtrName `
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
function Invoke-Entry([string[]]$Args) {
    Ensure-Container

    # -t needs a real console; drop it when output is redirected.
    $tty = -not ([Console]::IsInputRedirected -or [Console]::IsOutputRedirected)

    # Quote any argument containing whitespace so PowerShell 5.1's native
    # argument passing does not re-split it.
    $quoted = foreach ($a in $Args) {
        if ($null -ne $a -and $a -match '[\s"]') {
            '"' + ($a -replace '"', '\"') + '"'
        } else { $a }
    }

    if ($tty) {
        docker exec -it -w /src $CtrName /entrypoint.sh @quoted
    } else {
        docker exec -i -w /src $CtrName /entrypoint.sh @quoted
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
