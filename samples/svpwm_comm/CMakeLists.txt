cmake_minimum_required(VERSION 3.20.0)

# Point the build at the HMI module (this sample lives inside it).
set(ZEPHYR_EXTRA_MODULES ${CMAKE_CURRENT_SOURCE_DIR}/../..)

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(svpwm_comm LANGUAGES C)

# Sources are role-gated: the host (G474) runs SVPWM + the comm host peer
# and no LVGL; the device (H743) runs the comm device handlers and the
# full display stack. main.c dispatches on the same symbols.
target_sources(app PRIVATE src/main.c)
target_sources_ifdef(CONFIG_SVPWM_COMM_ROLE_HOST app PRIVATE
	src/control.c src/spi_cmd.c src/uart_comm.c)
target_sources_ifdef(CONFIG_SVPWM_COMM_ROLE_DEVICE app PRIVATE
	src/comm_device.c)

target_include_directories(app PRIVATE include)