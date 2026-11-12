cmake_minimum_required(VERSION 3.20)
project(HWVX-SDL VERSION 0.1.0 LANGUAGES C)

# Shown in packages; CI passes the git tag or commit.
set(HWVX_VERSION "${PROJECT_VERSION}" CACHE STRING "Version string for packages")
# Desktop/flatpak/metainfo/Android application id.
set(HWVX_APP_ID "io.github.alaxend0l.hwvx")

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_EXTENSIONS ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE RelWithDebInfo)
endif()

if(NOT CMAKE_C_COMPILER_ID STREQUAL "GNU")
    message(WARNING "HWVX-SDL is developed with GCC; ${CMAKE_C_COMPILER_ID} is untested")
endif()

find_package(Threads REQUIRED)

# TLS for joining online games through a tunnel (src/port/net.c). A pinned release: the
# Flatpak builds offline and hands over the same archive (FETCHCONTENT_SOURCE_DIR_MBEDTLS).
include(FetchContent)
set(ENABLE_PROGRAMS OFF CACHE BOOL "" FORCE)
set(ENABLE_TESTING OFF CACHE BOOL "" FORCE)
set(MBEDTLS_FATAL_WARNINGS OFF CACHE BOOL "" FORCE)
set(DISABLE_PACKAGE_CONFIG_AND_INSTALL ON CACHE BOOL "" FORCE)
FetchContent_Declare(mbedtls
    URL https://github.com/Mbed-TLS/mbedtls/releases/download/mbedtls-3.6.7/mbedtls-3.6.7.tar.bz2
    URL_HASH SHA256=a7e8bcbec0e6f761b4af24f25677626b35f762f68eef79c08677a363212d11f6)
FetchContent_MakeAvailable(mbedtls)

# The built-in mixed-build recipe (src/port/mix.c), as bytes.
set(HWVX_GEN_DIR ${CMAKE_BINARY_DIR}/gen)
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS ${CMAKE_SOURCE_DIR}/recipes/ultimate.jsonc)
file(READ ${CMAKE_SOURCE_DIR}/recipes/ultimate.jsonc HWVX_RECIPE_HEX HEX)
string(REGEX REPLACE "([0-9a-f][0-9a-f])" "0x\\1," HWVX_RECIPE_HEX "${HWVX_RECIPE_HEX}")
file(WRITE ${HWVX_GEN_DIR}/mix_recipe_ultimate.h.new
    "/* Generated from recipes/ultimate.jsonc (CMakeLists.txt). */\nstatic const char g_mixRecipeUltimate[] = { ${HWVX_RECIPE_HEX} };\n")
execute_process(COMMAND ${CMAKE_COMMAND} -E copy_if_different
    ${HWVX_GEN_DIR}/mix_recipe_ultimate.h.new ${HWVX_GEN_DIR}/mix_recipe_ultimate.h)
include_directories(${HWVX_GEN_DIR})

# Compressed audio for mods (src/audiodec over src/third_party's dr_wav/dr_flac/
# dr_mp3 and stb_vorbis), linked into the game and built as the helper hwvx-audiodec that
# tools/xdl_media.py and tools/xdl_mod.py run.
add_library(hwvx_audiodec STATIC ${CMAKE_SOURCE_DIR}/src/audiodec/audiodec.c)
target_include_directories(hwvx_audiodec PUBLIC ${CMAKE_SOURCE_DIR}/src/audiodec)
set_target_properties(hwvx_audiodec PROPERTIES POSITION_INDEPENDENT_CODE ON)
target_compile_options(hwvx_audiodec PRIVATE -w)
if(NOT WIN32)
    target_link_libraries(hwvx_audiodec PUBLIC m)
endif()
if(NOT ANDROID)
    add_executable(hwvx-audiodec ${CMAKE_SOURCE_DIR}/src/audiodec/main.c ${CMAKE_SOURCE_DIR}/src/audiodec/stb_vorbis_impl.c)
    target_link_libraries(hwvx-audiodec PRIVATE hwvx_audiodec)
endif()

file(GLOB GAME_SOURCES CONFIGURE_DEPENDS ${CMAKE_SOURCE_DIR}/src/game/*.c)
file(GLOB PORT_SOURCES CONFIGURE_DEPENDS ${CMAKE_SOURCE_DIR}/src/port/*.c)

if(ANDROID)
    # SDL's Java activity loads libSDL3.so and libmain.so. The Gradle build
    # (android/) passes the SDL source tree, built here alongside the game.
    set(HWVX_SDL_SOURCE_DIR "" CACHE PATH "SDL3 source tree (Android)")
    add_subdirectory(${HWVX_SDL_SOURCE_DIR} SDL EXCLUDE_FROM_ALL)
    add_library(hwvx SHARED ${GAME_SOURCES} ${PORT_SOURCES})
    set_target_properties(hwvx PROPERTIES OUTPUT_NAME main)
else()
    # Shared or static SDL3, whichever the prefix has (release builds link it
    # statically: see packaging/build-sdl3.sh).
    find_package(SDL3 REQUIRED CONFIG)
    add_executable(hwvx ${GAME_SOURCES} ${PORT_SOURCES})
endif()
target_include_directories(hwvx PRIVATE
    ${CMAKE_SOURCE_DIR}/src/game/include
    ${CMAKE_SOURCE_DIR}/src/port)
target_link_libraries(hwvx PRIVATE SDL3::SDL3 Threads::Threads mbedtls hwvx_audiodec ${CMAKE_DL_LIBS})
if(NOT WIN32)
    target_link_libraries(hwvx PRIVATE m)
endif()
if(WIN32)
    target_link_libraries(hwvx PRIVATE ws2_32)   # src/port/net.c
endif()
if(MINGW)
    # One .exe with nothing beside it: a MinGW whose threads are POSIX ones
    # (Homebrew's, MSYS2's) would otherwise import libwinpthread-1.dll for
    # _Thread_local (Port_Mix.c, xdl.c), which Windows doesn't have.
    target_link_options(hwvx PRIVATE -static)
endif()

target_compile_options(hwvx PRIVATE
    # The decomp type-puns everywhere and relies on two's-complement wrap.
    -fno-strict-aliasing
    -fwrapv
    -Wall
    -Wno-unknown-pragmas
    -Wno-unused-variable
    -Wno-unused-but-set-variable
    -Wno-unused-function
    -Wno-parentheses
    -Wno-missing-braces
    -Wno-pointer-sign
    -Wno-char-subscripts
    # 64-bit hosts: a pointer squeezed into a 32-bit slot must go through
    # PTR()/HND() (see src/port/port.h), so these are hard errors.
    -Werror=int-to-pointer-cast
    -Werror=pointer-to-int-cast
    -Werror=implicit-function-declaration
    -Werror=return-type)

# Game sources stay close to the decomp; don't drown real issues in its style.
set_source_files_properties(${GAME_SOURCES} PROPERTIES COMPILE_OPTIONS
    "-Wno-unused-value;-Wno-maybe-uninitialized;-Wno-sign-compare;-Wno-incompatible-pointer-types")

# game_protos.h hides a file's own functions that use its file-local types.
foreach(src ${GAME_SOURCES})
    get_filename_component(stem ${src} NAME_WE)
    string(MAKE_C_IDENTIFIER ${stem} stem)
    set_property(SOURCE ${src} APPEND PROPERTY COMPILE_DEFINITIONS HWVX_SRC_${stem})
endforeach()

# Debug aid for the data pack byte-swapping: see src/port/swapcheck.c.
option(HWVX_SWAPCHECK "Report game reads that disagree with how pack data was swapped" OFF)
if(HWVX_SWAPCHECK)
    target_compile_definitions(hwvx PRIVATE HWVX_SWAPCHECK)
    set_property(SOURCE ${GAME_SOURCES} APPEND PROPERTY COMPILE_OPTIONS
        "-fsanitize=kernel-address;--param=asan-instrumentation-with-call-threshold=0;--param=asan-stack=0;--param=asan-globals=0")
endif()

option(HWVX_ASAN "Build with AddressSanitizer/UBSan" OFF)
if(HWVX_ASAN)
    target_compile_options(hwvx PRIVATE -fsanitize=address,undefined -fno-omit-frame-pointer)
    target_link_options(hwvx PRIVATE -fsanitize=address,undefined)
endif()

# ---- tests -----------------------------------------------------------------
# tools/test_*.c drive the GX API against a stub renderer, so they need no GPU
# and no game data. Opt-in: they compile the game sources a second time. Each
# test brings its own main() and replaces the window entry point with
# tools/<test>_stubs.c.
option(HWVX_TESTS "Build the tests in tools/" OFF)
if(HWVX_TESTS AND NOT ANDROID)
    enable_testing()
    set(HWVX_TEST_PORT_SOURCES ${PORT_SOURCES})
    list(REMOVE_ITEM HWVX_TEST_PORT_SOURCES ${CMAKE_SOURCE_DIR}/src/port/main_sdl.c)
    file(GLOB HWVX_TEST_SOURCES CONFIGURE_DEPENDS ${CMAKE_SOURCE_DIR}/tools/test_*.c)
    foreach(src ${HWVX_TEST_SOURCES})
        if(src MATCHES "_stubs\\.c$")
            continue()
        endif()
        get_filename_component(stem ${src} NAME_WE)
        set(stubs ${CMAKE_SOURCE_DIR}/tools/${stem}_stubs.c)
        if(NOT EXISTS ${stubs})
            set(stubs "")
        endif()
        add_executable(${stem} ${src} ${stubs} ${GAME_SOURCES} ${HWVX_TEST_PORT_SOURCES})
        target_include_directories(${stem} PRIVATE
            ${CMAKE_SOURCE_DIR}/src/game/include
            ${CMAKE_SOURCE_DIR}/src/port)
        target_link_libraries(${stem} PRIVATE SDL3::SDL3 Threads::Threads mbedtls hwvx_audiodec ${CMAKE_DL_LIBS})
        if(NOT WIN32)
            target_link_libraries(${stem} PRIVATE m)
        endif()
        target_compile_options(${stem} PRIVATE
            -fno-strict-aliasing
            -fwrapv
            -Wall
            -Wno-unknown-pragmas
            -Wno-unused-variable
            -Wno-unused-but-set-variable
            -Wno-unused-function
            -Wno-parentheses
            -Wno-missing-braces
            -Wno-pointer-sign
            -Wno-char-subscripts
            -Werror=int-to-pointer-cast
            -Werror=pointer-to-int-cast
            -Werror=implicit-function-declaration
            -Werror=return-type)
        if(WIN32)
            target_link_libraries(${stem} PRIVATE ws2_32)
        endif()
        target_compile_definitions(${stem} PRIVATE HWVX_SOURCE_DIR="${CMAKE_SOURCE_DIR}")
        add_test(NAME ${stem} COMMAND ${stem})
        set_tests_properties(${stem} PROPERTIES SKIP_RETURN_CODE 77)
    endforeach()

    # The online transport needs neither the game nor a window. Not named
    # test_*: it links only its own files.
    add_executable(nettest tools/nettest.c src/port/net.c src/port/net_ws.c src/port/net_sim.c)
    target_include_directories(nettest PRIVATE ${CMAKE_SOURCE_DIR}/src/port)
    target_link_libraries(nettest PRIVATE SDL3::SDL3 mbedtls)
    if(WIN32)
        target_link_libraries(nettest PRIVATE ws2_32)
    endif()
    target_compile_options(nettest PRIVATE -Wall -Wextra -Wno-unused-parameter -Wno-missing-field-initializers)
    if(HWVX_ASAN)
        target_compile_options(nettest PRIVATE -fsanitize=address,undefined -fno-omit-frame-pointer)
        target_link_options(nettest PRIVATE -fsanitize=address,undefined)
    endif()
    # A headless player for testing online sessions: tools/netbot.c.
    add_executable(netbot tools/netbot.c src/port/net.c src/port/net_ws.c src/port/net_sim.c)
    target_include_directories(netbot PRIVATE ${CMAKE_SOURCE_DIR}/src/port ${CMAKE_SOURCE_DIR}/src/game/include)
    target_link_libraries(netbot PRIVATE SDL3::SDL3 mbedtls)
    if(WIN32)
        target_link_libraries(netbot PRIVATE ws2_32)
    endif()
    target_compile_options(netbot PRIVATE -Wall -Wextra -Wno-unused-parameter -Wno-missing-field-initializers)
    add_test(NAME nettest_selftest COMMAND nettest selftest)
    add_test(NAME nettest_e2e COMMAND nettest e2e)

    # The JSON save store (src/game/Port_Saves.c): its own files, no game or
    # window. tools/savetest.c says what it covers.
    add_executable(savetest tools/savetest.c src/game/Port_Saves.c src/game/Port_SaveFormat.c
        src/game/Port_SaveIds.c src/game/Port_Content.c src/port/jsonc.c src/port/fileio.c)
    target_include_directories(savetest PRIVATE
        ${CMAKE_SOURCE_DIR}/src/game/include
        ${CMAKE_SOURCE_DIR}/src/port)
    target_link_libraries(savetest PRIVATE SDL3::SDL3 mbedtls)
    target_compile_options(savetest PRIVATE -fno-strict-aliasing -fwrapv -Wall -Wno-unknown-pragmas
        -Wno-unused-function -Wno-missing-braces -Wno-pointer-sign -Wno-char-subscripts
        -Werror=implicit-function-declaration -Werror=return-type)
    if(HWVX_ASAN)
        target_compile_options(savetest PRIVATE -fsanitize=address,undefined -fno-omit-frame-pointer)
        target_link_options(savetest PRIVATE -fsanitize=address,undefined)
    endif()
    add_test(NAME savetest COMMAND savetest savetest-out)

    # The .xdl pack cache's file handling (src/port/xdl.c): its own files, no
    # game or window. tools/xdltest.c says what it covers.
    add_executable(xdltest tools/xdltest.c src/port/xdl.c src/port/fileio.c)
    target_include_directories(xdltest PRIVATE
        ${CMAKE_SOURCE_DIR}/src/game/include
        ${CMAKE_SOURCE_DIR}/src/port)
    target_link_libraries(xdltest PRIVATE SDL3::SDL3)
    target_compile_options(xdltest PRIVATE -fno-strict-aliasing -fwrapv -Wall -Wno-unknown-pragmas
        -Wno-unused-function -Wno-missing-braces -Wno-pointer-sign
        -Werror=implicit-function-declaration -Werror=return-type)
    if(HWVX_ASAN)
        target_compile_options(xdltest PRIVATE -fsanitize=address,undefined -fno-omit-frame-pointer)
        target_link_options(xdltest PRIVATE -fsanitize=address,undefined)
    endif()
    add_test(NAME xdltest COMMAND xdltest xdltest-out)

    # Importing the releases' saves (src/game/Port_SaveImport.c). The example
    # saves are read from HWVX-SDL/build-saves and orig/ when they are there
    # (a worktree sits one folder deeper); tools/saveimporttest.c says what
    # it covers.
    add_executable(saveimporttest tools/saveimporttest.c src/game/Port_SaveImport.c src/game/Port_Saves.c
        src/game/Port_SaveFormat.c src/game/Port_SaveIds.c src/game/Port_Content.c src/port/jsonc.c src/port/fileio.c)
    target_include_directories(saveimporttest PRIVATE
        ${CMAKE_SOURCE_DIR}/src/game/include
        ${CMAKE_SOURCE_DIR}/src/port)
    target_link_libraries(saveimporttest PRIVATE SDL3::SDL3 mbedtls)
    target_compile_options(saveimporttest PRIVATE -fno-strict-aliasing -fwrapv -Wall -Wno-unknown-pragmas
        -Wno-unused-function -Wno-missing-braces -Wno-pointer-sign -Wno-char-subscripts
        -Werror=implicit-function-declaration -Werror=return-type)
    if(HWVX_ASAN)
        target_compile_options(saveimporttest PRIVATE -fsanitize=address,undefined -fno-omit-frame-pointer)
        target_link_options(saveimporttest PRIVATE -fsanitize=address,undefined)
    endif()
    set(HWVX_SAVEIMPORT_ARGS saveimporttest-out)
    foreach(dir ${CMAKE_SOURCE_DIR}/build-saves ${CMAKE_SOURCE_DIR}/../build-saves)
        if(EXISTS ${dir}/gc AND NOT HWVX_SAVEIMPORT_SAMPLES)
            get_filename_component(HWVX_SAVEIMPORT_SAMPLES ${dir} ABSOLUTE)
            list(APPEND HWVX_SAVEIMPORT_ARGS --samples ${HWVX_SAVEIMPORT_SAMPLES})
        endif()
    endforeach()
    foreach(dir ${CMAKE_SOURCE_DIR}/../orig ${CMAKE_SOURCE_DIR}/../../orig)
        if(EXISTS ${dir}/ps2-memcard AND NOT HWVX_SAVEIMPORT_ORIG)
            get_filename_component(HWVX_SAVEIMPORT_ORIG ${dir} ABSOLUTE)
            list(APPEND HWVX_SAVEIMPORT_ARGS --orig ${HWVX_SAVEIMPORT_ORIG})
        endif()
    endforeach()
    add_test(NAME saveimporttest COMMAND saveimporttest ${HWVX_SAVEIMPORT_ARGS})

    # Port_RemoveTree never follows a link (src/port/fileio.c).
    add_executable(removetreetest tools/removetreetest.c src/port/fileio.c)
    target_include_directories(removetreetest PRIVATE ${CMAKE_SOURCE_DIR}/src/game/include ${CMAKE_SOURCE_DIR}/src/port)
    target_link_libraries(removetreetest PRIVATE SDL3::SDL3)
    add_test(NAME removetreetest COMMAND removetreetest)
    set_tests_properties(removetreetest PROPERTIES SKIP_RETURN_CODE 77)
endif()

# ---- platforms and packaging (see packaging/README.md) ---------------------

# Renderer used without --renderer: opengl, vulkan, metal or d3d12. Empty: the
# platform's own (d3d12 on Windows, metal on macOS, vulkan elsewhere; see
# main_sdl.c). One that fails to start falls back to OpenGL (except on
# Android, always vulkan).
set(HWVX_DEFAULT_RENDERER "" CACHE STRING "Default renderer (empty: the platform's)")
if(HWVX_DEFAULT_RENDERER)
    target_compile_definitions(hwvx PRIVATE HWVX_DEFAULT_RENDERER="${HWVX_DEFAULT_RENDERER}")
endif()

if(WIN32)
    # A GUI program: no console window. Logs go to SDL's log output; errors
    # show in a message box (Port_Fatal).
    option(HWVX_CONSOLE "Windows: keep the console window for logs" OFF)
    if(NOT HWVX_CONSOLE)
        set_target_properties(hwvx PROPERTIES WIN32_EXECUTABLE TRUE)
    endif()
endif()

if(APPLE AND NOT IOS)
    option(HWVX_MACOS_BUNDLE "Build Hot Wheels Velocity X.app" OFF)
    if(HWVX_MACOS_BUNDLE)
        set(MACOSX_BUNDLE_ICON_FILE hwvx.icns)
        set(HWVX_ICNS ${CMAKE_BINARY_DIR}/hwvx.icns)
        if(EXISTS ${HWVX_ICNS})
            target_sources(hwvx PRIVATE ${HWVX_ICNS})
            set_source_files_properties(${HWVX_ICNS} PROPERTIES MACOSX_PACKAGE_LOCATION Resources)
        endif()
        set_target_properties(hwvx PROPERTIES
            MACOSX_BUNDLE TRUE
            OUTPUT_NAME "Hot Wheels Velocity X"
            MACOSX_BUNDLE_INFO_PLIST ${CMAKE_SOURCE_DIR}/packaging/macos/Info.plist.in
            MACOSX_BUNDLE_GUI_IDENTIFIER ${HWVX_APP_ID}
            MACOSX_BUNDLE_BUNDLE_VERSION ${HWVX_VERSION}
            MACOSX_BUNDLE_SHORT_VERSION_STRING ${HWVX_VERSION})
    endif()
endif()

# hwvx-launcher: the same program, opening the launcher (src/port/launcher.c)
# instead of booting into the game. App bundles use --launcher instead.
if(NOT ANDROID AND NOT HWVX_MACOS_BUNDLE)
    add_custom_command(TARGET hwvx POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy $<TARGET_FILE:hwvx>
                $<TARGET_FILE_DIR:hwvx>/hwvx-launcher$<TARGET_FILE_SUFFIX:hwvx>
        VERBATIM)
endif()

if(UNIX AND NOT APPLE AND NOT ANDROID)
    # For AppImage and Flatpak: a standard prefix layout.
    include(GNUInstallDirs)
    configure_file(packaging/linux/hwvx.desktop.in ${CMAKE_BINARY_DIR}/${HWVX_APP_ID}.desktop @ONLY)
    configure_file(packaging/linux/hwvx.metainfo.xml.in ${CMAKE_BINARY_DIR}/${HWVX_APP_ID}.metainfo.xml @ONLY)
    install(TARGETS hwvx hwvx-audiodec RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
    install(PROGRAMS $<TARGET_FILE_DIR:hwvx>/hwvx-launcher DESTINATION ${CMAKE_INSTALL_BINDIR})
    install(FILES ${CMAKE_BINARY_DIR}/${HWVX_APP_ID}.desktop DESTINATION ${CMAKE_INSTALL_DATADIR}/applications)
    install(FILES ${CMAKE_BINARY_DIR}/${HWVX_APP_ID}.metainfo.xml DESTINATION ${CMAKE_INSTALL_DATADIR}/metainfo)
    # A PNG too: AppStream (Flatpak) and appimagetool (.DirIcon) want one.
    # packaging/icon-256.png: rsvg-convert -w 256 -h 256 packaging/icon.svg
    install(FILES packaging/icon.svg DESTINATION ${CMAKE_INSTALL_DATADIR}/icons/hicolor/scalable/apps
            RENAME ${HWVX_APP_ID}.svg)
    install(FILES packaging/icon-256.png DESTINATION ${CMAKE_INSTALL_DATADIR}/icons/hicolor/256x256/apps
            RENAME ${HWVX_APP_ID}.png)
endif()
