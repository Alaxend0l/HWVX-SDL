cmake_minimum_required(VERSION 3.20)
project(HWVX-SDL LANGUAGES C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_EXTENSIONS ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE RelWithDebInfo)
endif()

if(NOT CMAKE_C_COMPILER_ID STREQUAL "GNU")
    message(WARNING "HWVX-SDL is developed with GCC; ${CMAKE_C_COMPILER_ID} is untested")
endif()

find_package(SDL3 REQUIRED CONFIG)
find_package(Threads REQUIRED)

file(GLOB GAME_SOURCES CONFIGURE_DEPENDS ${CMAKE_SOURCE_DIR}/src/game/*.c)
file(GLOB PORT_SOURCES CONFIGURE_DEPENDS ${CMAKE_SOURCE_DIR}/src/port/*.c)

add_executable(hwvx ${GAME_SOURCES} ${PORT_SOURCES})
target_include_directories(hwvx PRIVATE
    ${CMAKE_SOURCE_DIR}/src/game/include
    ${CMAKE_SOURCE_DIR}/src/port
    ${CMAKE_SOURCE_DIR}/thirdparty)
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

set_source_files_properties(${CMAKE_SOURCE_DIR}/src/port/miniaudio_impl.c PROPERTIES COMPILE_OPTIONS "-w")

option(HWVX_ASAN "Build with AddressSanitizer/UBSan" OFF)
if(HWVX_ASAN)
    target_compile_options(hwvx PRIVATE -fsanitize=address,undefined -fno-omit-frame-pointer)
    target_link_options(hwvx PRIVATE -fsanitize=address,undefined)
endif()
