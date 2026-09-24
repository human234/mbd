<#
.SYNOPSIS
    Verifies (and explains how to regenerate) the STM32CubeProgrammer tree
    tarball that the HMI Docker image bakes in.

.DESCRIPTION
    The image needs docker\stm32cubeprog-tree.tar.gz, a pruned, pre-built
    install tree (bin/ without the GUI's jre, lib/, Data_Base/, Drivers/).
    It is produced ONCE on a Linux machine from ST's official installer and
    cached here out of git - the IzPack installer cannot run headless
    (`File execution failed`), so we do not run it inside the image build.

    To (re)generate the tarball on a Linux machine:
      1. Download the STM32CubeProgrammer "Linux 64" zip from
         https://www.st.com/en/development-tools/stm32cubeprog.html
         (browser + SLA0048 licence accepted), unzip it, then run its
         installer silently on a DESKTOP session:
             unzip SetupSTM32CubeProgrammer_linux_64.zip
             jre/bin/java -jar SetupSTM32CubeProgrammer-2.23.0.exe \
                 <path-to>\auto-install.xml
         (docker\auto-install.xml in this repo is a ready-made one; it
          installs to /usr/local/STMicroelectronics/STM32Cube/
          STM32CubeProgrammer)
      2. Prune and tar (bin\ minus jre, plus lib, Data_Base, Drivers):
             cd /usr/local/STMicroelectronics/STM32Cube
             rsync -a --exclude jre STM32CubeProgrammer/bin  /tmp/tree/STM32CubeProgrammer/
             rsync -a STM32CubeProgrammer/{lib,Data_Base,Drivers} /tmp/tree/STM32CubeProgrammer/
             tar -C /tmp/tree -czf $PSScriptRoot\stm32cubeprog-tree.tar.gz STM32CubeProgrammer

    After the tarball is in place, rebuild the image (build context = this
    repo root so the Dockerfile COPY path resolves):
        docker build -f docker\Dockerfile -t ghcr.io/human234/hmi-build:0.2.0 .

.EXAMPLE
    PS> .\fetch-stm32cubeprog.ps1
#>
param(
    [string]$Out = (Join-Path $PSScriptRoot "stm32cubeprog-tree.tar.gz")
)

$ErrorActionPreference = "Stop"

if ((Test-Path $Out) -and ((Get-Item $Out).Length -gt 100MB)) {
    Write-Host "OK: $Out is already present (size $([math]::Round((Get-Item $Out).Length / 1MB)) MB)."
    exit 0
}

Write-Host @"

docker\stm32cubeprog-tree.tar.gz is missing. See the header comment of this
script for how to (re)generate it from ST's installer on a Linux machine.

Then rebuild the image:
  docker build -f docker\Dockerfile -t ghcr.io/human234/hmi-build:0.2.0 .
"@