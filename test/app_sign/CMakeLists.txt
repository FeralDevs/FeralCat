cmake_minimum_required(VERSION 3.16)
project(app_sign_tests LANGUAGES C CXX)
enable_testing()
set(ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../..")
set(MONOCYPHER "${ROOT}/lib/monocypher/src")
add_executable(app_sign_test app_sign_test.cpp "${ROOT}/src/system/app_sign.cpp"
  "${MONOCYPHER}/monocypher.c" "${MONOCYPHER}/monocypher-ed25519.c")
target_include_directories(app_sign_test PRIVATE "${MONOCYPHER}" "${ROOT}/src/system")
set_target_properties(app_sign_test PROPERTIES C_STANDARD 99 CXX_STANDARD 11 CXX_STANDARD_REQUIRED YES)
if(MSVC)
  target_compile_definitions(app_sign_test PRIVATE _CRT_SECURE_NO_WARNINGS)
  target_compile_options(app_sign_test PRIVATE /W4)
else()
  target_compile_options(app_sign_test PRIVATE -Wall -Wextra -Werror)
endif()
add_test(NAME app_sign_firmware COMMAND app_sign_test
  "${ROOT}/sd files/apps/meowplayer/app.elf"
  "${ROOT}/sd files/apps/meowplayer/app.elf.sig"
  "${CMAKE_CURRENT_SOURCE_DIR}/fixtures/development-message.bin"
  "${CMAKE_CURRENT_SOURCE_DIR}/fixtures/development-message.bin.sig")
find_package(Python3 COMPONENTS Interpreter)
if(Python3_FOUND)
  add_subdirectory("${ROOT}/tools/app_signing" "signer")
  add_test(NAME app_sign_cli COMMAND "${Python3_EXECUTABLE}"
    "${CMAKE_CURRENT_SOURCE_DIR}/signer_test.py" "$<TARGET_FILE:meowsign>"
    "${ROOT}/tools/app_signing/nes-development.pub"
    "${CMAKE_CURRENT_SOURCE_DIR}/fixtures/development-message.bin"
    "${CMAKE_CURRENT_SOURCE_DIR}/fixtures/development-message.bin.sig")
endif()
