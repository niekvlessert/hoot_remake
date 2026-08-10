cmake_minimum_required(VERSION 3.16)

project(hoot_port
    VERSION 0.1.0
    DESCRIPTION "Headless Hoot replay core port"
    LANGUAGES C CXX)

include(GNUInstallDirs)

# Keep source/execution character sets deterministic on MSVC. Hoot's public
# metadata contract is UTF-8, including Japanese catalogue text.
if(MSVC)
    add_compile_options(/utf-8)
endif()

option(HOOT_CATALOG_ONLY "Configure only the JSON/SQLite catalogue build target" OFF)
find_package(Python3 COMPONENTS Interpreter REQUIRED)
set(HOOT_CATALOG_JSON "${CMAKE_BINARY_DIR}/catalog-src/hoot.catalog.json")
set(HOOT_CATALOG_SQLITE "${CMAKE_BINARY_DIR}/hoot.sqlite")
set(HOOT_CATALOG_ZSTD "${CMAKE_BINARY_DIR}/hoot.sqlite.zst")
add_custom_command(
    OUTPUT "${HOOT_CATALOG_JSON}" "${HOOT_CATALOG_SQLITE}" "${HOOT_CATALOG_ZSTD}"
    COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/tools/hootcatalog/hoot_catalog.py"
        build
        --input "${CMAKE_CURRENT_SOURCE_DIR}/xml_20251231.zip"
        --json-dir "${CMAKE_BINARY_DIR}/catalog-src"
        --overrides "${CMAKE_CURRENT_SOURCE_DIR}/hoot-overrides.xml"
        --sqlite "${HOOT_CATALOG_SQLITE}"
        --zstd "${HOOT_CATALOG_ZSTD}"
        --level 19
    DEPENDS
        "${CMAKE_CURRENT_SOURCE_DIR}/tools/hootcatalog/hoot_catalog.py"
        "${CMAKE_CURRENT_SOURCE_DIR}/xml_20251231.zip"
        "${CMAKE_CURRENT_SOURCE_DIR}/hoot-overrides.xml"
    COMMENT "Building JSON and zstd level 19 SQLite Hoot catalogues"
    VERBATIM)
add_custom_target(hoot_catalog
    DEPENDS "${HOOT_CATALOG_JSON}" "${HOOT_CATALOG_SQLITE}" "${HOOT_CATALOG_ZSTD}")

if(HOOT_CATALOG_ONLY)
    return()
endif()

if(EMSCRIPTEN)
    # Use Emscripten's maintained ports for the browser build. The web bundle
    # preloads the uncompressed SQLite catalogue, so zstd is deliberately not
    # linked into Wasm just to decompress one startup resource.
    add_library(hoot_web_zlib INTERFACE)
    target_compile_options(hoot_web_zlib INTERFACE "--use-port=zlib")
    target_link_options(hoot_web_zlib INTERFACE "--use-port=zlib")
    add_library(hoot_web_sqlite INTERFACE)
    target_compile_options(hoot_web_sqlite INTERFACE "--use-port=sqlite3")
    target_link_options(hoot_web_sqlite INTERFACE "--use-port=sqlite3")
    set(_HOOT_ZLIB_TARGET hoot_web_zlib)
    set(_HOOT_SQLITE_TARGET hoot_web_sqlite)
    set(_HOOT_ZSTD_TARGET "")
else()
    find_package(ZLIB REQUIRED)
    find_package(SQLite3 3.24 REQUIRED)
    find_path(ZSTD_INCLUDE_DIR NAMES zstd.h REQUIRED)
    find_library(ZSTD_LIBRARY NAMES zstd libzstd REQUIRED)
    add_library(ZSTD::ZSTD UNKNOWN IMPORTED)
    set_target_properties(ZSTD::ZSTD PROPERTIES
        IMPORTED_LOCATION "${ZSTD_LIBRARY}"
        INTERFACE_INCLUDE_DIRECTORIES "${ZSTD_INCLUDE_DIR}")
    set(_HOOT_ZLIB_TARGET ZLIB::ZLIB)
    set(_HOOT_SQLITE_TARGET SQLite::SQLite3)
    set(_HOOT_ZSTD_TARGET ZSTD::ZSTD)
endif()
if(NOT EMSCRIPTEN)
    find_package(Iconv QUIET)
endif()

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

set(HOOT_REQUIRED_VENDOR_SOURCES
    third_party/libkss/modules/kmz80/kmz80.c
    third_party/libkss/modules/kmz80/kmz80c.c
    third_party/libkss/modules/kmz80/kmz80t.c
    third_party/libkss/modules/kmz80/kmevent.c
    third_party/libvgm/emu/cores/ay8910.c
    third_party/libvgm/emu/cores/fmopn.c
    third_party/libvgm/emu/cores/ym2151.c
    third_party/libvgm/emu/cores/nukedopm.c
    third_party/libvgm/emu/cores/ymdeltat.c
    third_party/libvgm/emu/cores/fmopl.c
    third_party/libvgm/emu/cores/ymf262.c
    third_party/libvgm/emu/logging.c
    third_party/px68k-libretro/m68000/musashi/m68kcpu.c
    third_party/px68k-libretro/m68000/musashi/m68kops.c
    third_party/px68k-libretro/m68000/musashi/softfloat/softfloat.c)
foreach(vendor_source IN LISTS HOOT_REQUIRED_VENDOR_SOURCES)
    if(NOT EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/${vendor_source}")
        message(FATAL_ERROR
            "Missing vendored dependency source: ${vendor_source}. "
            "Run 'bash tools/ci/fetch_dependencies.sh' before configuring Hoot.")
    endif()
endforeach()

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
    third_party/libvgm/emu/cores/fmopl.c
    third_party/libvgm/emu/cores/ymf262.c
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

set(HOOT_CORE_SOURCES
    src/config/hoot_catalog.cpp
    src/config/hoot_catalog_loader.cpp
    src/config/hoot_json_loader.cpp
    src/config/hoot_sqlite_loader.cpp
    src/config/hoot_xml_loader.cpp
    src/core/hoot_context.cpp
    src/core/entry_validation.cpp
    src/core/text_encoding.cpp
    src/cpu/kmz80_cpu.cpp
    src/cpu/x86_cpu.cpp
    src/drivers/driver_registry.cpp
    src/drivers/microcabin_pc88_driver.cpp
    src/drivers/pc88_generic_driver.cpp
    src/drivers/microcabin_pc98dos_driver.cpp
    src/drivers/pc98_dos_driver.cpp
    src/drivers/x68k_generic_driver.cpp
    src/drivers/x68k_mxdrv_driver.cpp
    src/io/d88_image.cpp
    src/io/filesystem_asset_provider.cpp
    src/io/memory_asset_provider.cpp
    src/io/zip_archive.cpp
    src/sound/libvgm_ym2151.cpp
    src/sound/libvgm_okim6258.cpp
    src/sound/libvgm_ym2203.cpp
    src/sound/libvgm_opl.cpp
    src/sound/libvgm_ym2608.cpp
    src/sound/pc98_pcm86.cpp
    src/sound/pc98_beep.cpp
    src/sound/pc98_mpu401.cpp
    src/sound/x68k_pcm8_mixer.cpp
    src/sound/x68k_midi_transport.cpp
    src/sound/fluidsynth_midi_synth.cpp
    src/sound/vermouth_midi_synth.cpp
    src/sound/cm32p_midi_synth.cpp
    src/sound/cm64_midi_synth.cpp
    src/sound/mt32emu_midi_synth.cpp
    src/sound/nuked_sc55_clap_midi_synth.cpp)

set(_HOOT_ICONV_TARGET "")
if(Iconv_FOUND)
    set(_HOOT_ICONV_TARGET Iconv::Iconv)
endif()

add_library(hootcore_objects OBJECT ${HOOT_CORE_SOURCES})
set_target_properties(hootcore_objects PROPERTIES POSITION_INDEPENDENT_CODE ON)
target_compile_definitions(hootcore_objects PRIVATE HOOT_STATIC)
if(EMSCRIPTEN)
    target_compile_definitions(hootcore_objects PRIVATE HOOT_NO_ZSTD=1)
endif()
if(Iconv_FOUND)
    target_compile_definitions(hootcore_objects PRIVATE HOOT_HAVE_ICONV=1)
endif()
target_link_libraries(hootcore_objects PUBLIC
    ${_HOOT_ZLIB_TARGET} ${_HOOT_SQLITE_TARGET} ${_HOOT_ZSTD_TARGET} ${_HOOT_ICONV_TARGET}
    kmz80 libvgm_ym2203 px68k_musashi ${CMAKE_DL_LIBS})

add_library(hootcore STATIC $<TARGET_OBJECTS:hootcore_objects>)
target_compile_definitions(hootcore PUBLIC HOOT_STATIC)

add_library(hoot SHARED $<TARGET_OBJECTS:hootcore_objects>)
add_library(Hoot::hoot ALIAS hoot)
set_target_properties(hoot PROPERTIES
    OUTPUT_NAME hoot
    VERSION ${PROJECT_VERSION}
    SOVERSION 0
    WINDOWS_EXPORT_ALL_SYMBOLS ON)

target_include_directories(hootcore_objects PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_include_directories(hootcore PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_include_directories(hoot PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/src>
    $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>)
target_compile_features(hootcore_objects PUBLIC cxx_std_17)
target_compile_features(hootcore PUBLIC cxx_std_17)
target_compile_features(hoot PUBLIC cxx_std_17)

target_link_libraries(hootcore PUBLIC
    ${_HOOT_ZLIB_TARGET} ${_HOOT_SQLITE_TARGET} ${_HOOT_ZSTD_TARGET} ${_HOOT_ICONV_TARGET}
    kmz80 libvgm_ym2203 px68k_musashi ${CMAKE_DL_LIBS})
target_link_libraries(hoot PRIVATE
    ${_HOOT_ZLIB_TARGET} ${_HOOT_SQLITE_TARGET} ${_HOOT_ZSTD_TARGET} ${_HOOT_ICONV_TARGET}
    kmz80 libvgm_ym2203 px68k_musashi ${CMAKE_DL_LIBS})

if(MSVC)
    target_compile_options(hootcore_objects PRIVATE /W4)
else()
    target_compile_options(hootcore_objects PRIVATE -Wall -Wextra -pedantic)
endif()

# Frontend-only INI handling stays outside libhoot. It translates the user-facing
# hootplay.ini into the environment knobs understood by optional replay/synth
# backends and is shared by the CLI and SDL GUI.
add_library(hootappconfig STATIC
    src/config/hootplay_config.cpp
    src/config/hoot_app_paths.cpp
    src/config/hoot_settings.cpp)
target_include_directories(hootappconfig PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_compile_features(hootappconfig PUBLIC cxx_std_17)
if(MSVC)
    target_compile_options(hootappconfig PRIVATE /W4)
else()
    target_compile_options(hootappconfig PRIVATE -Wall -Wextra -pedantic)
endif()

option(HOOT_BUILD_GUI "Build the SDL3 graphical Hoot frontend" ON)
if(EMSCRIPTEN)
    set(_HOOT_WEB_DEFAULT ON)
else()
    set(_HOOT_WEB_DEFAULT OFF)
endif()
option(HOOT_BUILD_WEB "Enable Emscripten/WASM frontend settings" ${_HOOT_WEB_DEFAULT})

# SDL3 remains optional for CLI-only builds. A graphical Hoot build is
# deliberately stricter: Japanese/UTF-8 text is a core requirement of Hoot,
# therefore hootui MUST have both SDL3 and SDL3_ttf. Never silently produce an
# ASCII-only GUI binary.
set(_HOOT_SDL_TARGET "")
set(_HOOT_SDL_TTF_TARGET "")
set(HOOT_GUI_FONT_FILE "" CACHE FILEPATH "Japanese-capable TTF/OTF/TTC font to preload for the WASM GUI")
if(EMSCRIPTEN)
    add_library(hoot_sdl3 INTERFACE)
    target_compile_options(hoot_sdl3 INTERFACE "--use-port=sdl3")
    target_link_options(hoot_sdl3 INTERFACE "--use-port=sdl3")
    set(_HOOT_SDL_TARGET hoot_sdl3)

    if(HOOT_BUILD_GUI)
        # Browsers do not have a filesystem-visible system font we can pass to
        # SDL3_ttf. A Web GUI must therefore preload a Japanese-capable font.
        if(NOT HOOT_GUI_FONT_FILE)
            message(FATAL_ERROR
                "HOOT_BUILD_GUI=ON under Emscripten requires HOOT_GUI_FONT_FILE. "
                "Provide a Japanese-capable TTF/OTF/TTC font; Hoot does not bundle font files.")
        endif()
        if(NOT EXISTS "${HOOT_GUI_FONT_FILE}")
            message(FATAL_ERROR "HOOT_GUI_FONT_FILE does not exist: ${HOOT_GUI_FONT_FILE}")
        endif()
        add_library(hoot_sdl3_ttf INTERFACE)
        target_compile_options(hoot_sdl3_ttf INTERFACE "--use-port=sdl3_ttf")
        target_link_options(hoot_sdl3_ttf INTERFACE "--use-port=sdl3_ttf")
        set(_HOOT_SDL_TTF_TARGET hoot_sdl3_ttf)
    endif()
else()
    if(HOOT_BUILD_GUI)
        find_package(SDL3 3.2 CONFIG REQUIRED)
        find_package(SDL3_ttf 3.0 CONFIG REQUIRED)
        set(_HOOT_SDL_TARGET SDL3::SDL3)
        set(_HOOT_SDL_TTF_TARGET SDL3_ttf::SDL3_ttf)
        message(STATUS "hootui: SDL3 + SDL3_ttf found; Japanese UTF-8 renderer is mandatory")
    else()
        # Keep SDL audio available to hootplay where possible, without making
        # SDL a dependency of libhoot/CLI-only source builds.
        find_package(SDL3 3.2 CONFIG QUIET)
        if(SDL3_FOUND)
            set(_HOOT_SDL_TARGET SDL3::SDL3)
        endif()
    endif()
endif()

if(HOOT_BUILD_GUI)
    if(_HOOT_SDL_TARGET)
        add_executable(hootgui
            apps/hootgui/main.cpp
            apps/hootgui/retro_renderer.cpp
            apps/hootgui/spectrum.cpp
            apps/hootgui/wav_recorder.cpp)
        if(NOT EMSCRIPTEN)
            set_target_properties(hootgui PROPERTIES OUTPUT_NAME "hootui")
        endif()
        if(EMSCRIPTEN)
            target_link_libraries(hootgui PRIVATE hootcore hootappconfig ${_HOOT_SDL_TARGET})
        else()
            target_link_libraries(hootgui PRIVATE hoot hootappconfig ${_HOOT_SDL_TARGET})
        endif()
        if(_HOOT_SDL_TTF_TARGET)
            target_link_libraries(hootgui PRIVATE ${_HOOT_SDL_TTF_TARGET})
            target_compile_definitions(hootgui PRIVATE HOOT_HAVE_SDL3_TTF=1)
            if(EMSCRIPTEN)
                target_compile_definitions(hootgui PRIVATE HOOT_DEFAULT_UI_FONT="/fonts/hoot-ui-font")
            endif()
        endif()
        target_include_directories(hootgui PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/apps/hootgui)
        target_compile_features(hootgui PRIVATE cxx_std_17)
        if(NOT EMSCRIPTEN)
            add_custom_command(TARGET hootgui POST_BUILD
                COMMAND ${CMAKE_COMMAND} -E copy_if_different
                    ${CMAKE_CURRENT_SOURCE_DIR}/apps/hootgui/hootui_icon.bmp
                    $<TARGET_FILE_DIR:hootgui>/hootui_icon.bmp)
        endif()
        if(MSVC)
            target_compile_options(hootgui PRIVATE /W4)
        else()
            target_compile_options(hootgui PRIVATE -Wall -Wextra -pedantic)
        endif()
        if(EMSCRIPTEN OR HOOT_BUILD_WEB)
            if(NOT EMSCRIPTEN)
                message(FATAL_ERROR "HOOT_BUILD_WEB requires an Emscripten toolchain")
            endif()
            set_target_properties(hootgui PROPERTIES OUTPUT_NAME "hootweb" SUFFIX ".html")
            # Source distributions intentionally ship the compact production
            # catalogue, not the much larger upstream XML import archive. Build
            # the browser's uncompressed SQLite preload directly from that
            # production .zst file so a clean checkout/archive is sufficient.
            find_program(HOOT_ZSTD_CLI NAMES zstd REQUIRED)
            set(HOOT_WEB_CATALOG_SQLITE "${CMAKE_BINARY_DIR}/hoot-web.sqlite")
            add_custom_command(
                OUTPUT "${HOOT_WEB_CATALOG_SQLITE}"
                COMMAND "${HOOT_ZSTD_CLI}" -d -f
                    "${CMAKE_CURRENT_SOURCE_DIR}/catalog/hoot.sqlite.zst"
                    -o "${HOOT_WEB_CATALOG_SQLITE}"
                DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/catalog/hoot.sqlite.zst"
                COMMENT "Preparing uncompressed SQLite catalogue for Hoot Web"
                VERBATIM)
            add_custom_target(hoot_web_catalog DEPENDS "${HOOT_WEB_CATALOG_SQLITE}")
            add_dependencies(hootgui hoot_web_catalog)
            target_link_options(hootgui PRIVATE
                "--shell-file=${CMAKE_CURRENT_SOURCE_DIR}/apps/hootgui/web/hoot_shell.html"
                "--preload-file=${HOOT_WEB_CATALOG_SQLITE}@/catalog/hoot.sqlite"
                "-lidbfs.js"
                "-sALLOW_MEMORY_GROWTH=1"
                "-sFORCE_FILESYSTEM=1"
                "-sEXPORTED_RUNTIME_METHODS=FS,ccall")
            # Recent Emscripten releases may expose growing Wasm memory as a
            # resizable ArrayBuffer. Chrome rejects views backed by that buffer
            # in WebGL texSubImage2D. Patch only the generated memory accessor;
            # the Wasm memory can still grow normally.
            find_program(HOOT_NODE_EXECUTABLE NAMES node REQUIRED)
            add_custom_command(TARGET hootgui POST_BUILD
                COMMAND "${HOOT_NODE_EXECUTABLE}"
                    "${CMAKE_CURRENT_SOURCE_DIR}/tools/patch_emscripten_webgl.mjs"
                    "$<TARGET_FILE_DIR:hootgui>/hootweb.js"
                COMMENT "Applying Chrome WebGL compatibility to Hoot Web"
                VERBATIM)
            if(HOOT_GUI_FONT_FILE)
                target_link_options(hootgui PRIVATE
                    "--preload-file=${HOOT_GUI_FONT_FILE}@/fonts/hoot-ui-font")
            endif()
        endif()
    else()
        message(FATAL_ERROR "HOOT_BUILD_GUI=ON requires SDL3 and SDL3_ttf")
    endif()
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

if(EMSCRIPTEN)
    target_link_libraries(hootplay PRIVATE hootcore hootappconfig)
else()
    target_link_libraries(hootplay PRIVATE hoot hootappconfig)
endif()
if(APPLE)
    target_link_libraries(hootplay PRIVATE "-framework AudioToolbox")
elseif(_HOOT_SDL_TARGET)
    target_link_libraries(hootplay PRIVATE ${_HOOT_SDL_TARGET})
    target_compile_definitions(hootplay PRIVATE HOOT_HAVE_SDL3=1)
endif()

if(MSVC)
    target_compile_options(hootplay PRIVATE /W4)
else()
    target_compile_options(hootplay PRIVATE -Wall -Wextra -pedantic)
endif()

add_executable(hootprobe
    tools/hootprobe/main.cpp)

target_link_libraries(hootprobe PRIVATE hootcore)

if(MSVC)
    target_compile_options(hootprobe PRIVATE /W4)
else()
    target_compile_options(hootprobe PRIVATE -Wall -Wextra -pedantic)
endif()


include(CTest)
if(BUILD_TESTING)
    add_executable(hoot_c_api_test tests/hoot_c_api_test.c)
    target_link_libraries(hoot_c_api_test PRIVATE hoot)
    add_test(NAME hoot_c_api COMMAND hoot_c_api_test)

    add_executable(driver_registry_test tests/driver_registry_test.cpp)
    target_link_libraries(driver_registry_test PRIVATE hootcore)
    add_test(NAME driver_registry COMMAND driver_registry_test)

    add_executable(x68k_memory_test tests/x68k_memory_test.cpp)
    target_link_libraries(x68k_memory_test PRIVATE hootcore)
    add_test(NAME x68k_memory COMMAND x68k_memory_test)

    add_executable(x68k_pcm8_mixer_test tests/x68k_pcm8_mixer_test.cpp)
    target_link_libraries(x68k_pcm8_mixer_test PRIVATE hootcore)
    add_test(NAME x68k_pcm8_mixer COMMAND x68k_pcm8_mixer_test)

    add_executable(x68k_midi_transport_test tests/x68k_midi_transport_test.cpp)
    target_link_libraries(x68k_midi_transport_test PRIVATE hootcore)
    add_test(NAME x68k_midi_transport COMMAND x68k_midi_transport_test)

    add_library(mock_nuked_sc55_clap MODULE tests/mock_nuked_sc55_clap.cpp)
    target_include_directories(mock_nuked_sc55_clap PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
    target_compile_features(mock_nuked_sc55_clap PRIVATE cxx_std_17)
    set_target_properties(mock_nuked_sc55_clap PROPERTIES PREFIX "" SUFFIX ".clap" OUTPUT_NAME "Nuked-SC55")

    add_executable(nuked_sc55_clap_synth_test tests/nuked_sc55_clap_synth_test.cpp)
    target_link_libraries(nuked_sc55_clap_synth_test PRIVATE hootcore)
    add_test(NAME nuked_sc55_clap_synth COMMAND nuked_sc55_clap_synth_test $<TARGET_FILE:mock_nuked_sc55_clap>)

    add_library(mock_mt32emu MODULE tests/mock_mt32emu.cpp)
    set_target_properties(mock_mt32emu PROPERTIES PREFIX "" OUTPUT_NAME "mock_mt32emu")

    add_library(mock_mdxmini MODULE tests/mock_mdxmini.cpp)
    set_target_properties(mock_mdxmini PROPERTIES PREFIX "" OUTPUT_NAME "mock_mdxmini")

    add_library(mock_vermouth_fluid MODULE tests/mock_vermouth.cpp)
    set_target_properties(mock_vermouth_fluid PROPERTIES PREFIX "" OUTPUT_NAME "mock_vermouth_fluid")

    add_library(mock_vermouth_legacy MODULE tests/mock_vermouth.cpp)
    target_compile_definitions(mock_vermouth_legacy PRIVATE MOCK_VERMOUTH_LEGACY=1)
    set_target_properties(mock_vermouth_legacy PROPERTIES PREFIX "" OUTPUT_NAME "mock_vermouth_legacy")

    add_executable(vermouth_midi_synth_test tests/vermouth_midi_synth_test.cpp)
    target_link_libraries(vermouth_midi_synth_test PRIVATE hootcore)
    add_dependencies(vermouth_midi_synth_test mock_vermouth_fluid mock_vermouth_legacy)
    add_test(NAME vermouth_midi_synth COMMAND vermouth_midi_synth_test
        $<TARGET_FILE:mock_vermouth_fluid> $<TARGET_FILE:mock_vermouth_legacy>)

    add_executable(x68k_dmaint_test tests/x68k_dmaint_test.cpp)
    target_link_libraries(x68k_dmaint_test PRIVATE hootcore)
    add_test(NAME x68k_dmaint COMMAND x68k_dmaint_test ${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures)

    add_executable(x68k_mxdrv_driver_test tests/x68k_mxdrv_driver_test.cpp)
    target_link_libraries(x68k_mxdrv_driver_test PRIVATE hootcore)
    add_test(NAME x68k_mxdrv_driver COMMAND x68k_mxdrv_driver_test ${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures $<TARGET_FILE:mock_mdxmini>)

    add_executable(mt32emu_midi_synth_test tests/mt32emu_midi_synth_test.cpp)
    target_link_libraries(mt32emu_midi_synth_test PRIVATE hootcore)
    add_test(NAME mt32emu_midi_synth COMMAND mt32emu_midi_synth_test $<TARGET_FILE:mock_mt32emu>)

    add_executable(cm32p_midi_synth_test tests/cm32p_midi_synth_test.cpp)
    target_link_libraries(cm32p_midi_synth_test PRIVATE hootcore)
    add_test(NAME cm32p_midi_synth COMMAND cm32p_midi_synth_test $<TARGET_FILE:mock_mt32emu>)

    add_executable(hootplay_config_test tests/hootplay_config_test.cpp)
    target_link_libraries(hootplay_config_test PRIVATE hootappconfig)
    add_test(NAME hootplay_config COMMAND hootplay_config_test)

    add_executable(hoot_app_paths_settings_test tests/hoot_app_paths_settings_test.cpp)
    target_link_libraries(hoot_app_paths_settings_test PRIVATE hootappconfig)
    add_test(NAME hoot_app_paths_settings COMMAND hoot_app_paths_settings_test)

    add_executable(hoot_user_overrides_test tests/hoot_user_overrides_test.cpp)
    target_link_libraries(hoot_user_overrides_test PRIVATE hootcore)
    add_test(NAME hoot_user_overrides COMMAND hoot_user_overrides_test)

    add_executable(utf8_util_test tests/utf8_util_test.cpp)
    target_include_directories(utf8_util_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
    target_compile_features(utf8_util_test PRIVATE cxx_std_17)
    add_test(NAME utf8_util COMMAND utf8_util_test)

    add_executable(hoot_xml_unicode_test tests/hoot_xml_unicode_test.cpp)
    target_link_libraries(hoot_xml_unicode_test PRIVATE hootcore)
    add_test(NAME hoot_xml_unicode COMMAND hoot_xml_unicode_test)

    add_executable(hoot_legacy_sjis_test tests/hoot_legacy_sjis_test.cpp)
    target_link_libraries(hoot_legacy_sjis_test PRIVATE hootcore)
    add_test(NAME hoot_legacy_sjis COMMAND hoot_legacy_sjis_test ${CMAKE_CURRENT_SOURCE_DIR}/hoot.xml)

    add_executable(pc88_generic_driver_test tests/pc88_generic_driver_test.cpp)
    target_link_libraries(pc88_generic_driver_test PRIVATE hootcore)
    add_test(NAME pc88_generic_driver COMMAND pc88_generic_driver_test ${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures)

    add_executable(pc98_bare_driver_test tests/pc98_bare_driver_test.cpp)
    target_link_libraries(pc98_bare_driver_test PRIVATE hootcore)
    add_test(NAME pc98_bare_driver COMMAND pc98_bare_driver_test ${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures)

    add_executable(pc98dos_generic_driver_test tests/pc98dos_generic_driver_test.cpp)
    target_link_libraries(pc98dos_generic_driver_test PRIVATE hootcore)
    add_test(NAME pc98dos_generic_driver COMMAND pc98dos_generic_driver_test ${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures)

    add_executable(pc98_special_boards_test tests/pc98_special_boards_test.cpp)
    target_link_libraries(pc98_special_boards_test PRIVATE hootcore)
    add_test(NAME pc98_special_boards COMMAND pc98_special_boards_test ${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures)

    add_executable(libvgm_opl_test tests/libvgm_opl_test.cpp)
    target_link_libraries(libvgm_opl_test PRIVATE hootcore)
    add_test(NAME libvgm_opl COMMAND libvgm_opl_test)

    add_executable(pc98_pcm86_test tests/pc98_pcm86_test.cpp)
    target_link_libraries(pc98_pcm86_test PRIVATE hootcore)
    add_test(NAME pc98_pcm86 COMMAND pc98_pcm86_test)

    add_executable(pc98_beep_test tests/pc98_beep_test.cpp)
    target_link_libraries(pc98_beep_test PRIVATE hootcore)
    add_test(NAME pc98_beep COMMAND pc98_beep_test)

    add_executable(pc98_mpu401_test tests/pc98_mpu401_test.cpp)
    target_link_libraries(pc98_mpu401_test PRIVATE hootcore)
    add_test(NAME pc98_mpu401 COMMAND pc98_mpu401_test)

    add_executable(pc98_midi_driver_test tests/pc98_midi_driver_test.cpp)
    target_link_libraries(pc98_midi_driver_test PRIVATE hootcore)
    add_test(NAME pc98_midi_driver COMMAND pc98_midi_driver_test ${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures $<TARGET_FILE:mock_mt32emu>)

    add_executable(spectrum_analyzer_test tests/spectrum_analyzer_test.cpp apps/hootgui/spectrum.cpp)
    target_include_directories(spectrum_analyzer_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/apps/hootgui)
    target_compile_features(spectrum_analyzer_test PRIVATE cxx_std_17)
    add_test(NAME spectrum_analyzer COMMAND spectrum_analyzer_test)

    add_executable(wav_recorder_test tests/wav_recorder_test.cpp apps/hootgui/wav_recorder.cpp)
    target_include_directories(wav_recorder_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
    target_compile_features(wav_recorder_test PRIVATE cxx_std_17)
    add_test(NAME wav_recorder COMMAND wav_recorder_test)

    # The test sources use assert() as executable checks. Keep those checks
    # enabled even when the project itself is configured as Release.
    foreach(test_target driver_registry_test x68k_memory_test x68k_pcm8_mixer_test x68k_dmaint_test x68k_midi_transport_test x68k_mxdrv_driver_test vermouth_midi_synth_test nuked_sc55_clap_synth_test mt32emu_midi_synth_test cm32p_midi_synth_test hootplay_config_test hoot_app_paths_settings_test hoot_user_overrides_test utf8_util_test hoot_xml_unicode_test hoot_legacy_sjis_test pc88_generic_driver_test pc98_bare_driver_test pc98dos_generic_driver_test pc98_special_boards_test libvgm_opl_test pc98_pcm86_test pc98_beep_test pc98_mpu401_test pc98_midi_driver_test spectrum_analyzer_test wav_recorder_test)
        if(MSVC)
            target_compile_options(${test_target} PRIVATE /UNDEBUG)
        else()
            target_compile_options(${test_target} PRIVATE -UNDEBUG)
        endif()
    endforeach()
endif()

include(CMakePackageConfigHelpers)
if(APPLE)
    set(_HOOT_INSTALL_RPATH "@loader_path/../${CMAKE_INSTALL_LIBDIR}")
elseif(UNIX)
    set(_HOOT_INSTALL_RPATH "\$ORIGIN/../${CMAKE_INSTALL_LIBDIR}")
endif()
if(DEFINED _HOOT_INSTALL_RPATH)
    set_target_properties(hootplay PROPERTIES INSTALL_RPATH "${_HOOT_INSTALL_RPATH}")
    if(TARGET hootgui)
        set_target_properties(hootgui PROPERTIES INSTALL_RPATH "${_HOOT_INSTALL_RPATH}")
    endif()
endif()
install(TARGETS hoot
    EXPORT HootTargets
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
    INCLUDES DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})
install(TARGETS hootplay hootprobe hoot2wav
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
if(TARGET hootgui)
    install(TARGETS hootgui RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
    install(FILES apps/hootgui/hootui_icon.bmp DESTINATION ${CMAKE_INSTALL_DATADIR}/hoot)
endif()
install(FILES
    src/core/hoot_api.h
    src/core/hoot_errors.h
    src/core/hoot_track_info.h
    src/core/hoot_visual_state.h
    src/core/hoot_library_info.h
    DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/hoot)

install(FILES docs/LIBHOOT.md docs/SDL_GUI.md docs/WASM.md docs/BUILD_FRONTENDS.md docs/INSTALLATION.md
    DESTINATION ${CMAKE_INSTALL_DOCDIR})

set(HOOT_CMAKE_INSTALL_DIR "${CMAKE_INSTALL_LIBDIR}/cmake/Hoot")
configure_package_config_file(
    cmake/HootConfig.cmake.in
    "${CMAKE_CURRENT_BINARY_DIR}/HootConfig.cmake"
    INSTALL_DESTINATION "${HOOT_CMAKE_INSTALL_DIR}")
write_basic_package_version_file(
    "${CMAKE_CURRENT_BINARY_DIR}/HootConfigVersion.cmake"
    VERSION "${PROJECT_VERSION}"
    COMPATIBILITY SameMajorVersion)
install(EXPORT HootTargets
    FILE HootTargets.cmake
    NAMESPACE Hoot::
    DESTINATION "${HOOT_CMAKE_INSTALL_DIR}")
install(FILES
    "${CMAKE_CURRENT_BINARY_DIR}/HootConfig.cmake"
    "${CMAKE_CURRENT_BINARY_DIR}/HootConfigVersion.cmake"
    DESTINATION "${HOOT_CMAKE_INSTALL_DIR}")

configure_file(cmake/hoot.pc.in "${CMAKE_CURRENT_BINARY_DIR}/hoot.pc" @ONLY)
install(FILES "${CMAKE_CURRENT_BINARY_DIR}/hoot.pc"
    DESTINATION "${CMAKE_INSTALL_LIBDIR}/pkgconfig")
