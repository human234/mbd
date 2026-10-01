# zccli — build and flash your screen without touching Zephyr

`zccli` hides the build system, the RTOS and the board configuration behind
three commands. You edit one `.c` file, run `build`, then `flash`.

Everything runs in a Docker container, so nothing has to be installed
besides Docker Desktop. Your source files stay on your PC; only the
toolchain lives in the container.

---

## What you need once

| | |
|---|---|
| **Docker Desktop** on Windows | https://www.docker.com/products/docker-desktop/ |
| **STM32CubeProgrammer** | https://www.st.com/en/development-tools/stm32cubeprog.html — needed only to write the chip over SWD |
| **A USB cable** on the board's ST-LINK port | the mini-B / USB-C port next to the big USB port |
| **Power on** the board | the green `PWR` LED must be lit |

Then, from this folder:

```powershell
.\zccli.ps1 setup
```

This pulls the image if needed and starts a container named `zccli`, then
prints the two commands you will use. To confirm the whole environment run:

```powershell
.\zccli.ps1 doctor
```

which ends with `8 ok, 0 warning(s), 0 failure(s)` when everything is ready.

---

## Your daily loop

Put your screen code in one file in this folder, e.g. `main.c`:

```c
#include <lvgl.h>
#include <hmi/hmi.h>
#include "zccli.h"

void zc_user_init(lv_obj_t *root)
{
    static float speed = 42.0f;

    lv_obj_t *g = hmi_ui_gauge_create(root, 160, "Speed", "km/h");

    hmi_ui_gauge_set_range(g, 0, 120);
    hmi_ui_gauge_bind_value(g, &speed);
}
```

```powershell
.\zccli.ps1 build main.c     # compile it
.\zccli.ps1 flash            # write it to the board
```

`build` finishes by copying the firmware to `.\out\zephyr.hex`, and `flash`
writes exactly that file — so you always flash the result of the last build.
To write a firmware built elsewhere, name it:

```powershell
.\zccli.ps1 flash .\out\zephyr.hex
```

`flash` drives the ST-LINK over SWD using STM32CubeProgrammer and resets the
board when it is done. If it cannot find the probe, the three-step checklist
it prints is usually enough to sort it out.

`build` with no filename compiles every top-level `.c` in the folder, so you
can split a screen across several files.

---

## Commands

| Command | What it does |
|---|---|
| `.\zccli.ps1 setup` | one-time: start the container |
| `.\zccli.ps1 build <file.c> [...]` | compile your screen |
| `.\zccli.ps1 build <file.c> --board nucleo_g474re` | compile for the other board |
| `.\zccli.ps1 build <file.c> --clean` | throw away the cache and rebuild from scratch |
| `.\zccli.ps1 flash [path\to.hex]` | write the firmware over SWD |
| `.\zccli.ps1 export` | copy the finished firmware into `.\out\` |
| `.\zccli.ps1 size` | show flash/RAM usage |
| `.\zccli.ps1 clean` | delete the cached build |
| `.\zccli.ps1 doctor` | check everything end to end |
| `.\zccli.ps1 update` | pull the latest image |
| `.\zccli.ps1 shell` | open a shell in the container |
| `.\zccli.ps1 help` | this list |

Anything else is passed through to the container, so `.\zccli.ps1 west ...`
works if you ever need it.

### Choosing a board

The default is `nucleo_h743zi`. The other supported board is
`nucleo_g474re`. Each board has its own cache, so switching back and forth
does not force a rebuild.

| Board | Flash | RAM |
|---|---|---|
| `nucleo_h743zi` | 2 MB — typically **~22%** used | 512 KB — ~18% used |
| `nucleo_g474re` | 512 KB — typically **~88%** used | 128 KB — ~71% used |

The G474 is tight: a screen with several widgets can run out of flash or
RAM. If a build fails near the end with an "overflowed" error, switch to
the H743 or remove a widget.

---

## Where things live

| Path | What it is |
|---|---|
| `.\main.c` (or whatever you name it) | **your code** — the only file you edit |
| `.\out\zephyr.hex` | the finished firmware, produced by `export` |
| `.\zccli\app\` | the template project — lives inside the image, do not edit |
| `.\zccli\app\src\zc_core.c` | the frozen runtime — baked into the image, never modified |
| `.\zccli\app\src\user\` | where your `.c` file is copied on each build |
| container `/build` | build cache, kept between runs so builds are fast |

The first build takes about half a minute. After that it is a few seconds,
and only recompiles what changed.

---

## Troubleshooting

**`zccli build` says "no .c file given and none found"**
Run `.\zccli.ps1 doctor`. If it reports `no project folder mounted at /src`,
you started zccli from a different folder than the one holding your `.c`
files — `cd` into that folder first.

**`flash` cannot find the ST-LINK**
- Check the green `PWR` LED and try a different USB cable (some are
  power-only).
- Close STM32CubeIDE or any other program that may have claimed the probe.
- Run `.\zccli.ps1 doctor` for the full check.

**Builds are slow**
The cache lives in a Docker volume. If you deleted it, the first build
rebuilds everything — that is expected and only happens once.

**Something changed and now everything fails**
`.\zccli.ps1 update` pulls a fresh image; `.\zccli.ps1 build <file> --clean`
clears a stale cache between the two.

---

## For the maintainer: rebuilding the image

```bash
make zccli-stage     # prune workspace + ARM-only SDK into a staging dir
make zccli-image     # build the image
make zccli-push      # build and push to ghcr.io
```

or directly:

```bash
zccli/docker-build.sh                  # builds ghcr.io/human234/zccli:<VERSION>
IMAGE=zccli:dev zccli/docker-build.sh   # build under a different tag
PUSH=1 zccli/docker-build.sh            # build and push
```

The image is based on plain `ubuntu:24.04` and carries only what building
the two supported boards needs: the ARM target toolchain (no `hosttools`
sysroot), a pruned west workspace, and Zephyr's Python requirements.

`zccli/stage.sh` lists every prune and why it is safe. If a build starts
failing after a Zephyr or SDK update, re-run it with one prune disabled to
isolate the cause:

```bash
PRUNE_STM32CUBE=0 zccli/stage.sh /tmp/zccli-stage
PRUNE_ZEPHYR_TREE=0 PRUNE_STM32CUBE=0 PRUNE_LVGL=0 PRUNE_PICOLIBC=0 PRUNE_HOSTTOOLS=0 \
    zccli/stage.sh /tmp/zccli-stage   # unpruned: reproduces the full image
```

Known safe prune limits, verified by building both boards from a pruned
image:

- `zephyr/{tests,doc,samples}` and all `boards/` vendors except `st/`,
  `shields/`, `common/` — **but** every `Kconfig*` file under `boards/`
  must be kept, because `boards/Kconfig` unconditionally `rsource`s
  `espressif/common/Kconfig.qemu`.
- `stm32cube/` except `stm32g4xx/`, `stm32h7xx/`, `common_ll/` and the
  two top-level CMake files.
- `lvgl/{tests,examples,demos,docs}` and `picolibc/test`.
- The SDK's `hosttools/` directory entirely — Zephyr only appends it to
  `CMAKE_PREFIX_PATH`, so system `cmake`/`ninja`/`dtc` from apt replace it.
  Dropping it also removes the `/home/<user>` symlink hack the old image
  needed to work around its patched `PT_INTERP`.

Do **not** re-add `stm32cubeprog-tree.tar.gz` or any `*/build/` directory to
the workspace: `zccli` flashes from the host, and a baked `CMakeCache.txt`
carries absolute host paths that break `west build -p always`.
