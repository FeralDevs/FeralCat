cmake_minimum_required(VERSION 3.16)
project(nes_storage_tests LANGUAGES CXX)
enable_testing()

add_executable(nes_storage_test
    nes_storage_test.cpp
    ../../src/system/emulation/nes_rom.cpp
    ../../src/system/emulation/nes_save.cpp)
target_include_directories(nes_storage_test PRIVATE ../../src/system/emulation)
set_target_properties(nes_storage_test PROPERTIES CXX_STANDARD 11 CXX_STANDARD_REQUIRED YES CXX_EXTENSIONS NO)
if(MSVC)
    target_compile_options(nes_storage_test PRIVATE /W4 /WX)
else()
    target_compile_options(nes_storage_test PRIVATE -Wall -Wextra -Wpedantic -Werror)
endif()
add_test(NAME nes_storage COMMAND nes_storage_test)
