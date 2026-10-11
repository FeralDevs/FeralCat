cmake_minimum_required(VERSION 3.20)
project(nes_service_contract LANGUAGES CXX)
enable_testing()
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
add_executable(nes_service_test nes_service_test.cpp
  ../../src/system/emulation/nes_path.cpp
  ../../src/system/emulation/nes_rom.cpp
  ../../src/system/emulation/nes_save.cpp)
target_include_directories(nes_service_test PRIVATE . ../../src/system ../../lib/nes_core)
target_compile_definitions(nes_service_test PRIVATE MEOW_NES_SERVICE_HOST_TEST)
if(MSVC)
  target_compile_options(nes_service_test PRIVATE /W4 /WX)
  target_compile_definitions(nes_service_test PRIVATE _CRT_SECURE_NO_WARNINGS)
else()
  target_compile_options(nes_service_test PRIVATE -Wall -Wextra -Werror)
endif()
add_test(NAME nes_service_contract COMMAND nes_service_test)
set_tests_properties(nes_service_contract PROPERTIES TIMEOUT 15)
