cmake_minimum_required(VERSION 3.20.0)

# Point the build at the HMI module (this sample lives inside it).
set(ZEPHYR_EXTRA_MODULES ${CMAKE_CURRENT_SOURCE_DIR}/../..)

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(display_demo LANGUAGES C)

target_sources(app PRIVATE src/main.c)
