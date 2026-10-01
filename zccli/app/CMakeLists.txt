cmake_minimum_required(VERSION 3.20.0)

# The HMI module is not listed in the west manifest (it is attached to the
# workspace as the modules/hmi symlink), so it has to be named explicitly.
# This path resolves to the HMI module root both on the host (zccli/app)
# and in the image (/workdir/apps/HMI/zccli/app), which is why it is
# relative rather than hard-coded.
set(ZEPHYR_EXTRA_MODULES ${CMAKE_CURRENT_SOURCE_DIR}/../..)

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(zccli_app LANGUAGES C)

# Everything under src/ is compiled. src/zc_core.c is the frozen runtime;
# zccli copies your .c files into src/user/ before building.
#
# CONFIGURE_DEPENDS makes CMake re-run its glob on every build, so adding or
# removing a file does not need a hand-written source list or a -p always.
file(GLOB ZC_SOURCES CONFIGURE_DEPENDS
     ${CMAKE_CURRENT_SOURCE_DIR}/src/*.c
     ${CMAKE_CURRENT_SOURCE_DIR}/src/user/*.c)
target_sources(app PRIVATE ${ZC_SOURCES})

target_include_directories(app PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)
