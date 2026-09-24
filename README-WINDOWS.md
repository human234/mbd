# Using HMI on Windows 11 with Docker

Tested end-to-end on Windows 11 (Docker Desktop, WSL2 backend). This lets you
**build and flash** the HMI gauge demo without installing any Zephyr toolchain,
west, or Python. Everything runs inside the prebuilt Docker image
`ghcr.io/human234/hmi-build:0.2.0`, which bundles:

- the Zephyr SDK (1.0.1) — ARM-only subset,
- a **baked west workspace** at `/workdir` (zephyr + hal + lvgl + picolibc +
  the HMI app at `/workdir/apps/HMI`; `/workdir/modules/hmi` is a symlink to
  it), so no workspace mount is needed,
- the `STM32CubeProgrammer` CLI, so `west flash` works from inside the
  container.

The only thing required on Windows is **Docker Desktop**. For flashing, the
ST-Link probe must be passed into the WSL2 VM with `usbipd-win`.

## 1. Install prerequisites (one time)

Run in PowerShell (an admin window for `usbipd` steps):

```powershell
winget install -e --id Docker.DockerDesktop
winget install -e --id Microsoft.WSL
winget install -e --id dorssel.usbipd-win
```

Start **Docker Desktop** and wait for the whale icon to show it is running.
(The first `docker pull` below downloads the ~20 GB image, once.)

## 2. Pull the image

```powershell
docker pull ghcr.io/human234/hmi-build:0.2.0
```

## 3. Pass the ST-Link USB probe into Docker (WSL2)

Admin PowerShell:

```powershell
usbipd list                          # note the BUSID of the "ST-Link", e.g. 2-3
usbipd bind --busid <BUSID>          # only needed the first time
usbipd attach -w --busid <BUSID>     # attach to your default WSL distro
usbipd list                          # should now show "Attached"
```

> This version of `usbipd attach` uses `-w` (default WSL distribution) and has
> **no `--distribution` flag**. If a newer version offers it, the Docker
> Desktop target is `--distribution docker-desktop`.
>
> Re-run the `attach` after every reboot / Windows Update — the device is only
> visible while attached.

## 4. Start the persistent build container

```powershell
docker run --rm -d --privileged --name hmi-build `
  -w /workdir/apps/HMI `
  ghcr.io/human234/hmi-build:0.2.0 sleep infinity
```

`--privileged` lets the container see the passed-through USB probe (needed for
`west flash`). The container keeps its `/workdir/apps/HMI/build` between runs,
so repeat builds are incremental.

## 5. Build

```powershell
docker exec -w /workdir/apps/HMI hmi-build west build -b nucleo_g474re samples/gauge_demo
```

Expect: `FLASH: 495004 B (94.41%) / RAM: 96216 B (73.41%)`.
For a pristine build add `-p always`.

## 6. Flash

```powershell
docker exec -w /workdir/apps/HMI --user root hmi-build west flash
```

Runs `STM32_Programmer_CLI` as `root` so it can open the USB device nodes
(ours run as uid 1000 otherwise). Expect `File download complete` + `MCU Reset`.

## 7. Verify

- The gauge should animate (SPI + DMA path).
- Serial console: Device Manager → **Ports (COM & LPT)** →
  `STMicroelectronics STLink Virtual COM Port` (e.g. `COM3`); any terminal at
  **115200 8N1** shows the HMI app's FPS readout every 5 s.

## Helper scripts (optional)

The repo ships PowerShell wrappers for steps 4–6; they (re)create the
`hmi-build` container if it is missing, then use `docker exec`:

```powershell
.\build.ps1                          # docker exec hmi-build west build ...
.\flash.ps1                          # docker exec --user root hmi-build west flash
.\build.ps1 -Pristine                # rebuild from scratch
```

## Troubleshooting

| Symptom | Fix |
|---|---|
| `docker: unauthorized` on pull | `docker login ghcr.io` with a GitHub PAT (`read:packages`). |
| Pull is slow / 20 GB | Expected on first run; cached afterwards. |
| `usbipd list` shows no ST-Link | Rebind with `usbipd bind --busid <ID>`, or plug the board in again. |
| Flash fails with USB errors | Re-run `usbipd attach -w --busid <ID>` (drops on reboots/updates), verify `usbipd list` shows *Attached*, then `docker start hmi-build` and flash again. |
| `permission denied` on flash | Use `--user root` on the `west flash` exec (step 6). |
| `Cannot find a Zephyr workspace` | Do **not** run `west` on the Windows host; always exec inside the container. |
| Long pause on first build | First build of Zephyr + LVGL; follow-ups are incremental because the build dir persists in the container. |

## About the image

`ghcr.io/human234/hmi-build:0.2.0` is built from the official Zephyr CI base
(`ci-base:v0.29.3`, Zephyr 4.4.x / SDK 1.0.1) plus a pruned ARM toolchain, the
baked west workspace, and STM32CubeProgrammer v2.23.0. It is produced on Linux
from this repo with `make stage && make image` (see `docker/rebake-workspace.sh`
+ `docker/Dockerfile`) and published on ghcr.io.