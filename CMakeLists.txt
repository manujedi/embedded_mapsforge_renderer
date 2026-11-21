cmake_minimum_required(VERSION 3.12)

find_program(CCACHE_FOUND ccache)
if (CCACHE_FOUND)
   set_property(GLOBAL PROPERTY RULE_LAUNCH_COMPILE ccache)
endif ()

set(PICO_PLATFORM rp2350)
set(PICO_BOARD pico2)
set(PICO_SDK_PATH ${CMAKE_CURRENT_SOURCE_DIR}/pico-sdk/)
set(FREERTOS_KERNEL_PATH ${CMAKE_CURRENT_SOURCE_DIR}/FreeRTOS-Kernel)

include(${PICO_SDK_PATH}/external/pico_sdk_import.cmake)

project(cyclingPC ASM C CXX)

set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)

pico_sdk_init()

add_compile_options(-Wall
        -Wno-format          # int != int32_t as far as the compiler is concerned because gcc has int32_t as long int
        -Wno-unused-function # we have some for the docs that aren't called
)
if (CMAKE_C_COMPILER_ID STREQUAL "GNU")
    add_compile_options(-Wno-maybe-uninitialized)
endif ()

include(FreeRTOS_Kernel_import.cmake)


set(LV_CONF_PATH
        ${CMAKE_CURRENT_SOURCE_DIR}/lv_conf.h
        CACHE STRING "" FORCE)
include(lvgl/CMakeLists.txt)

# add freertos to lvgl
target_link_libraries(lvgl PRIVATE
        FreeRTOS-Kernel-Heap4
)

include_directories(./)
include_directories(./GUI)
include_directories(./Drivers)

include_directories(./Drivers/Touch)
include_directories(./Drivers/PSRAM)
include_directories(./Drivers/DISPLAY)
include_directories(./Drivers/SDCARD)
include_directories(./Drivers/FatFs)


add_executable(cyclingPC
        main.c
        ${CMAKE_CURRENT_LIST_DIR}/Drivers/PSRAM/psram.c
        Drivers/DISPLAY/display.c
        Drivers/SDCARD/sd_spi.c
        Drivers/FatFs/diskio.c
        Drivers/FatFs/ff.c
        Drivers/FatFs/ffsystem.c
        Drivers/FatFs/ffunicode.c
        Drivers/Touch/CST9217.c
        GUI/map_screen.c
        GUI/Fonts/JetBrainsMonoRegular_12.c
        mapsforge/mapsforge-renderer-queue.c
        mapsforge/mapsforge-renderer.c
        mapsforge/mapsforge_theme.c
        mapsforge/utils.c
        Drivers/GPS/LC76G.c
        Drivers/GPS/LC76G.h
)

target_include_directories(cyclingPC PRIVATE
        ${CMAKE_CURRENT_LIST_DIR}/
        ${CMAKE_CURRENT_LIST_DIR}/Drivers/PSRAM
)
target_link_libraries(cyclingPC PRIVATE
        pico_async_context_freertos
        FreeRTOS-Kernel-Heap4
        hardware_spi
        hardware_xip_cache
        hardware_pio
        hardware_dma
        hardware_i2c
        pico_stdlib
        lvgl
)
target_compile_definitions(cyclingPC PRIVATE
)

pico_enable_stdio_usb(cyclingPC 1)
pico_enable_stdio_uart(cyclingPC 0)
pico_generate_pio_header(cyclingPC ${CMAKE_CURRENT_LIST_DIR}/Drivers/DISPLAY/qspi.pio OUTPUT_DIR ${CMAKE_CURRENT_LIST_DIR}/Drivers/DISPLAY/)
#[[pico_generate_pio_header(cyclingPC ${CMAKE_CURRENT_LIST_DIR}/Drivers/GPS/uart_rx.pio OUTPUT_DIR ${CMAKE_CURRENT_LIST_DIR}/Drivers/GPS/)
pico_generate_pio_header(cyclingPC ${CMAKE_CURRENT_LIST_DIR}/Drivers/GPS/uart_tx.pio OUTPUT_DIR ${CMAKE_CURRENT_LIST_DIR}/Drivers/GPS/)]]

set_property(TARGET cyclingPC APPEND_STRING PROPERTY LINK_FLAGS
        "-Wl,--print-memory-usage"
)