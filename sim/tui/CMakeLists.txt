# Headless LovyanGFX renderer for MK_TUI app screens.
# Compiles the LovyanGFX v1 core into an off-screen sprite -> BMP. Windows can
# use the local timing-only platform adapter; other hosts retain SDL support.
cmake_minimum_required(VERSION 3.16)
project(meowkit-tui CXX C)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(FW_ROOT ${CMAKE_CURRENT_SOURCE_DIR}/../..)
set(LGFX ${FW_ROOT}/lib/LovyanGFX/src)

option(MEOWKIT_HEADLESS_PLATFORM "Use the sprite-only host platform without SDL windows" ${WIN32})
add_definitions(-DLGFX_SDL)

file(GLOB_RECURSE LGFX_SRC CONFIGURE_DEPENDS
    ${LGFX}/lgfx/Fonts/efont/*.c
    ${LGFX}/lgfx/Fonts/IPA/*.c
    ${LGFX}/lgfx/utility/*.c
    ${LGFX}/lgfx/v1/misc/*.cpp)
file(GLOB LGFX_CORE CONFIGURE_DEPENDS ${LGFX}/lgfx/v1/*.cpp)
list(APPEND LGFX_SRC ${LGFX_CORE}
    ${LGFX}/lgfx/v1/panel/Panel_Device.cpp
    ${LGFX}/lgfx/v1/panel/Panel_FrameBufferBase.cpp
    ${LGFX}/lgfx/v1/platforms/sdl/common.cpp)

if(MSVC)
    # MSVC cannot parse the two GNU variable-length arrays in gradient helpers.
    # Adapt allocation in a build-directory copy; the vendored source remains
    # untouched. TrackerUI does not use these gradient helpers.
    set(LGFX_BASE_SOURCE ${LGFX}/lgfx/v1/LGFXBase.cpp)
    set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS ${LGFX_BASE_SOURCE})
    file(READ ${LGFX_BASE_SOURCE} LGFX_BASE_HOST)
    string(REPLACE "rgb888_t scanline[w];"
        "std::unique_ptr<rgb888_t[]> scanlineStorage(new rgb888_t[w]); auto* scanline = scanlineStorage.get();"
        LGFX_BASE_HOST "${LGFX_BASE_HOST}")
    string(REPLACE "rgb888_t scanline[gradient_len];"
        "std::unique_ptr<rgb888_t[]> scanlineStorage(new rgb888_t[gradient_len]); auto* scanline = scanlineStorage.get();"
        LGFX_BASE_HOST "${LGFX_BASE_HOST}")
    file(WRITE ${CMAKE_CURRENT_BINARY_DIR}/LGFXBase_host.cpp "#include <memory>\n${LGFX_BASE_HOST}")
    list(REMOVE_ITEM LGFX_SRC ${LGFX_BASE_SOURCE})
    list(APPEND LGFX_SRC ${CMAKE_CURRENT_BINARY_DIR}/LGFXBase_host.cpp)
endif()

add_executable(meowkit-tui main.cpp ${LGFX_SRC})
target_include_directories(meowkit-tui PRIVATE
    ${LGFX}
    ${LGFX}/lgfx/v1
    ${FW_ROOT}/src/app
    ${FW_ROOT}/src/app/app_common)

if(MEOWKIT_HEADLESS_PLATFORM)
    # Only timing and the unused panel's opaque types are adapted. All pixels,
    # fonts and drawing primitives still use the vendored LovyanGFX core.
    target_include_directories(meowkit-tui PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/headless)
else()
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(SDL2 REQUIRED sdl2)
    target_include_directories(meowkit-tui PRIVATE ${SDL2_INCLUDE_DIRS})
    target_link_libraries(meowkit-tui PRIVATE ${SDL2_LIBRARIES})
    target_compile_options(meowkit-tui PRIVATE ${SDL2_CFLAGS_OTHER})
endif()
find_package(Threads REQUIRED)
target_link_libraries(meowkit-tui PRIVATE Threads::Threads)
if(MSVC)
    target_compile_definitions(meowkit-tui PRIVATE _CRT_SECURE_NO_WARNINGS NOMINMAX)
    target_compile_options(meowkit-tui PRIVATE /utf-8 /FI${CMAKE_CURRENT_SOURCE_DIR}/headless/msvc_compat.h)
endif()
