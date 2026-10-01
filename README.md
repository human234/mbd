# HMI

Zephyr module providing gauge-style HMI widgets and the display lifecycle
(`hmi_init` / `hmi_show`) on top of LVGL as a reusable library.

## Layout

```
zephyr/module.yml        module declaration (name: hmi)
CMakeLists.txt           zephyr_library_named(hmi)
Kconfig                  CONFIG_HMI gate + required LVGL selects
include/hmi/             public headers (single entry point: <hmi/hmi.h>)
src/                     library sources
samples/display_demo/    runnable example app (nucleo_g474re + ILI9488)
samples/motor_hmi/       panel + touch + LVGL app (nucleo_h743zi only)
samples/svpwm_comm/      two-role comm demo (host: G474RE, device: H743ZI)
zccli/                   end-user Docker toolchain: edit one .c file, then
                         `zccli.ps1 build` / `zccli.ps1 flash` (no Zephyr
                         install on the host). See zccli/README.md.
scripts/uart_host.py     host-side UART test tool for the comm protocol
Makefile                 zccli image wrappers (stage / image / push)
```

## Windows users (Docker)

No Zephyr SDK install is required: [`zccli/`](zccli/README.md) ships a
prebuilt image (`ghcr.io/human234/zccli`) with the toolchain and a baked,
pruned west workspace. You edit one `.c` file and run `.\zccli.ps1 build`
then `.\zccli.ps1 flash`; the source stays on the host, only the toolchain
runs in the container. See [zccli/README.md](zccli/README.md) for setup
(`setup`, `doctor`) and the daily loop.

## Public API

```c
#include <hmi/hmi.h>

int hmi_init(void);                 /* 0 / negative; display ready check */
void hmi_show(void);                /* blanking off + LVGL timer loop, never returns */

lv_obj_t *hmi_ui_gauge_create(lv_obj_t *parent, int size,
                              const char *title, const char *unit);
void hmi_ui_widget_set_pos(lv_obj_t *widget, int x, int y);
void hmi_ui_gauge_set_range(lv_obj_t *gauge, int32_t min, int32_t max);
void hmi_ui_gauge_bind_value(lv_obj_t *gauge, float *value);
void hmi_gauge_show_ui(lv_obj_t *parent);
extern uint32_t colorscheme[];
```

## Consuming the module

**Windows without any Zephyr install?** Read
[zccli/README.md](zccli/README.md) — builds via Docker only.

The easiest way is the ready-made west workspace manifest
([`human234/hmi-workspace`](https://github.com/human234/hmi-workspace)),
which pins Zephyr `v4.4.2` and installs this repo at `modules/hmi`:

```sh
west init -m https://github.com/human234/hmi-workspace hmi-workspace
cd hmi-workspace && west update
west build -p always -b nucleo_g474re modules/hmi/samples/display_demo
```

Or add the module to an existing west workspace by hand:

1. Make the module visible to your build:

   ```cmake
   set(ZEPHYR_EXTRA_MODULES <path-to-apps/HMI>)
   ```

   before `find_package(Zephyr REQUIRED)` in your app's `CMakeLists.txt`,
   or from the command line:

   ```sh
   west build -b nucleo_g474re <app> -- -DZEPHYR_EXTRA_MODULES=<path-to-apps/HMI>
   ```

2. Enable it in your `prj.conf`:

   ```ini
   CONFIG_HMI=y
   ```

   `CONFIG_HMI` selects the LVGL widgets, fonts, and LVGL itself. You still
   own the display-specific settings (e.g. `CONFIG_LV_COLOR_DEPTH_24`,
   `CONFIG_LV_Z_MEM_POOL_SIZE`, `CONFIG_LV_Z_VDB_SIZE`) and a `zephyr,display`
   chosen node; the module reads it out of your devicetree.

3. Use it:

   ```c
   #include <hmi/hmi.h>

   int main(void)
   {
       if (hmi_init() < 0) {
           return 0;
       }
       hmi_gauge_show_ui(lv_screen_active());
       hmi_show();
       return 0;   /* unreachable */
   }
   ```

## Build the bundled sample

```sh
west build -p always -b nucleo_g474re samples/display_demo
west flash -d build
```

`samples/motor_hmi` and `samples/svpwm_comm` also have their own `Makefile`
(`make` / `make flash` / `make clean`), which wrap the same `west` calls per
board/role.

## Notes

- Requires the LVGL module in the workspace (its Kconfig symbols are
  `select`ed by `CONFIG_HMI`).
- CPU-load / LVGL perf overlay is intentionally NOT selected by the module.
  If you enable `CONFIG_LV_USE_SYSMON`, also set
  `CONFIG_CPU_LOAD_BACKEND_RUNTIME_STATS=y` — the default idle-hook backend
  caused a boot-time fault on this board.