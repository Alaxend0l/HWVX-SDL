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
target_link_libraries(hwvx PRIVATE SDL3::SDL3 Threads::Threads ${CMAKE_DL_LIBS})
if(NOT WIN32)
    target_link_libraries(hwvx PRIVATE m)
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

# ---- platforms and packaging (see packaging/README.md) ---------------------

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

if(UNIX AND NOT APPLE AND NOT ANDROID)
    # For AppImage and Flatpak: a standard prefix layout.
    include(GNUInstallDirs)
    configure_file(packaging/linux/hwvx.desktop.in ${CMAKE_BINARY_DIR}/${HWVX_APP_ID}.desktop @ONLY)
    configure_file(packaging/linux/hwvx.metainfo.xml.in ${CMAKE_BINARY_DIR}/${HWVX_APP_ID}.metainfo.xml @ONLY)
    install(TARGETS hwvx RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
    install(FILES ${CMAKE_BINARY_DIR}/${HWVX_APP_ID}.desktop DESTINATION ${CMAKE_INSTALL_DATADIR}/applications)
    install(FILES ${CMAKE_BINARY_DIR}/${HWVX_APP_ID}.metainfo.xml DESTINATION ${CMAKE_INSTALL_DATADIR}/metainfo)
    install(FILES packaging/icon.svg DESTINATION ${CMAKE_INSTALL_DATADIR}/icons/hicolor/scalable/apps
            RENAME ${HWVX_APP_ID}.svg)
endif()
