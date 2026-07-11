cmake_minimum_required(VERSION 3.16)

project(hoot_port
    VERSION 0.1.0
    DESCRIPTION "Headless Hoot replay core port"
    LANGUAGES C CXX)

find_package(ZLIB REQUIRED)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_library(kmz80 STATIC
    third_party/libkss/modules/kmz80/kmz80.c
    third_party/libkss/modules/kmz80/kmz80c.c
    third_party/libkss/modules/kmz80/kmz80t.c
    third_party/libkss/modules/kmz80/kmevent.c)

target_include_directories(kmz80 PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/third_party/libkss/modules/kmz80)

add_library(libvgm_ym2203 STATIC
    third_party/libvgm/emu/cores/ay8910.c
    third_party/libvgm/emu/cores/fmopn.c
    third_party/libvgm/emu/cores/ym2151.c
    third_party/libvgm/emu/cores/nukedopm.c
    third_party/libvgm/emu/cores/ymdeltat.c
    third_party/libvgm/emu/logging.c)

target_include_directories(libvgm_ym2203 PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/third_party/libvgm
    ${CMAKE_CURRENT_SOURCE_DIR}/third_party/libvgm/emu
    ${CMAKE_CURRENT_SOURCE_DIR}/third_party/libvgm/emu/cores)

target_compile_definitions(libvgm_ym2203 PUBLIC HAVE_STDINT_H SNDDEV_YM2608)

add_library(px68k_musashi STATIC
    third_party/px68k-libretro/m68000/musashi/m68kcpu.c
    third_party/px68k-libretro/m68000/musashi/m68kops.c
    third_party/px68k-libretro/m68000/musashi/softfloat/softfloat.c)

target_include_directories(px68k_musashi PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/third_party/px68k-libretro/m68000/musashi
    ${CMAKE_CURRENT_SOURCE_DIR}/third_party/px68k-libretro/m68000/musashi/softfloat)

target_compile_definitions(px68k_musashi PUBLIC HAVE_STDINT_H)

add_library(hootcore STATIC
    src/config/hoot_catalog.cpp
    src/config/hoot_xml_loader.cpp
    src/core/hoot_context.cpp
    src/cpu/kmz80_cpu.cpp
    src/cpu/x86_cpu.cpp
    src/drivers/microcabin_pc88_driver.cpp
    src/drivers/microcabin_pc98dos_driver.cpp
    src/drivers/pc98_dos_driver.cpp
    src/drivers/x68k_generic_driver.cpp
    src/io/d88_image.cpp
    src/io/filesystem_asset_provider.cpp
    src/io/memory_asset_provider.cpp
    src/io/zip_archive.cpp
    src/sound/libvgm_ym2151.cpp
    src/sound/libvgm_okim6258.cpp
    src/sound/libvgm_ym2203.cpp
    src/sound/libvgm_ym2608.cpp)

target_include_directories(hootcore
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/src)

target_compile_features(hootcore PUBLIC cxx_std_17)
target_link_libraries(hootcore PUBLIC ZLIB::ZLIB kmz80 libvgm_ym2203 px68k_musashi)

if(MSVC)
    target_compile_options(hootcore PRIVATE /W4)
else()
    target_compile_options(hootcore PRIVATE -Wall -Wextra -pedantic)
endif()

add_executable(hoot2wav
    tools/hoot2wav/main.cpp
    tools/hoot2wav/wav_writer.cpp)

target_link_libraries(hoot2wav PRIVATE hootcore)

if(MSVC)
    target_compile_options(hoot2wav PRIVATE /W4)
else()
    target_compile_options(hoot2wav PRIVATE -Wall -Wextra -pedantic)
endif()

add_executable(hootplay
    tools/hootplay/main.cpp
    tools/hoot2wav/wav_writer.cpp)

target_link_libraries(hootplay PRIVATE hootcore)
if(APPLE)
    target_link_libraries(hootplay PRIVATE "-framework AudioToolbox")
endif()

if(MSVC)
    target_compile_options(hootplay PRIVATE /W4)
else()
    target_compile_options(hootplay PRIVATE -Wall -Wextra -pedantic)
endif()
