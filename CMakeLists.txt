cmake_minimum_required(VERSION 3.20.0)

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(hmi
        VERSION 0.1.0
        DESCRIPTION "simple HMI library"
        LANGUAGES C)

zephyr_library_named(hmi)
zephyr_include_directories(include)
zephyr_library_sources_ifdef(CONFIG_HMI src/hmi_app.c)
zephyr_library_sources_ifdef(CONFIG_HMI src/hmi_gauge.c)
zephyr_library_sources_ifdef(CONFIG_HMI src/hmi_button.c)
zephyr_library_sources_ifdef(CONFIG_HMI src/hmi_slider.c)
zephyr_library_sources_ifdef(CONFIG_HMI src/hmi_tabs.c)
zephyr_library_sources_ifdef(CONFIG_HMI src/hmi_chart.c)
zephyr_library_sources_ifdef(CONFIG_HMI src/hmi_polar.c)
zephyr_library_sources_ifdef(CONFIG_HMI src/hmi_screen.c)

zephyr_library_sources_ifdef(CONFIG_HMI_COMM src/hmi_comm.c)
zephyr_library_sources_ifdef(CONFIG_HMI_COMM_UART src/hmi_comm_uart.c)
