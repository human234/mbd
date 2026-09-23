cmake_minimum_required(VERSION 3.20.0)

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(hmi
        VERSION 0.1.0
        DESCRIPTION "simple HMI library"
        LANGUAGES C)

zephyr_library_named(hmi)
zephyr_include_directories(include)
target_sources(hmi PRIVATE
    src/hmi_app.c
    src/hmi_gauge.c
    src/hmi_button.c
    src/hmi_slider.c
    src/hmi_tabs.c
    src/hmi_chart.c
    src/hmi_polar.c
    src/hmi_screen.c
)
