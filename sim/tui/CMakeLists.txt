# Headless LovyanGFX renderer for MK_TUI app screens.
# Compiles the LovyanGFX v1 core + SDL platform (its Panel_sdl is the reference
# desktop target) and renders into an off-screen sprite -> BMP. SDL2 is linked
# because the vendored core's desktop build expects it, but no window is opened.
cmake_minimum_required(VERSION 3.16)
project(meowkit-tui CXX C)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(FW_ROOT ${CMAKE_CURRENT_SOURCE_DIR}/../..)
set(LGFX ${FW_ROOT}/lib/LovyanGFX/src)

add_definitions(-DLGFX_SDL)

file(GLOB_RECURSE LGFX_SRC CONFIGURE_DEPENDS
    ${LGFX}/lgfx/Fonts/efont/*.c
    ${LGFX}/lgfx/Fonts/IPA/*.c
    ${LGFX}/lgfx/utility/*.c
    ${LGFX}/lgfx/v1/*.cpp
    ${LGFX}/lgfx/v1/misc/*.cpp
    ${LGFX}/lgfx/v1/panel/Panel_Device.cpp
    ${LGFX}/lgfx/v1/panel/Panel_FrameBufferBase.cpp
    ${LGFX}/lgfx/v1/platforms/sdl/*.cpp)

add_executable(meowkit-tui main.cpp ${LGFX_SRC})
target_include_directories(meowkit-tui PRIVATE
    ${LGFX}
    ${FW_ROOT}/src/app
    ${FW_ROOT}/src/app/app_common)

find_package(PkgConfig REQUIRED)
pkg_check_modules(SDL2 REQUIRED sdl2)
target_include_directories(meowkit-tui PRIVATE ${SDL2_INCLUDE_DIRS})
target_link_libraries(meowkit-tui PRIVATE ${SDL2_LIBRARIES} pthread)
target_compile_options(meowkit-tui PRIVATE ${SDL2_CFLAGS_OTHER})
