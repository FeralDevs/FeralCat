cmake_minimum_required(VERSION 3.20)
project(meow_lua_tests LANGUAGES C CXX)
enable_testing()
set(CMAKE_C_STANDARD 99)
set(CMAKE_CXX_STANDARD 17)
set(ROOT "${CMAKE_CURRENT_LIST_DIR}/../..")
file(GLOB LUA_SOURCES CONFIGURE_DEPENDS "${ROOT}/lib/lua-5.5.1/src/*.c")
list(FILTER LUA_SOURCES EXCLUDE REGEX "/(lua|luac)\\.c$")
add_library(lua_test_engine STATIC ${LUA_SOURCES})
target_include_directories(lua_test_engine PUBLIC "${ROOT}/lib/lua-5.5.1/src")
target_compile_definitions(lua_test_engine PUBLIC LUAI_MAXCCALLS=32)
if(MSVC)
  target_compile_definitions(lua_test_engine PRIVATE _CRT_SECURE_NO_WARNINGS)
endif()
add_executable(runtime_test runtime_test.cpp "${ROOT}/src/system/lua_apps/lua_runtime.cpp"
  "${ROOT}/src/system/lua_apps/app_manifest.cpp")
target_include_directories(runtime_test PRIVATE "${ROOT}/src/system/lua_apps")
target_compile_definitions(runtime_test PRIVATE MEOW_TEST_ROOT="${ROOT}")
target_link_libraries(runtime_test PRIVATE lua_test_engine)
add_test(NAME runtime COMMAND runtime_test)
set_tests_properties(runtime PROPERTIES TIMEOUT 15)
add_executable(manifest_test manifest_test.cpp "${ROOT}/src/system/lua_apps/app_manifest.cpp")
target_include_directories(manifest_test PRIVATE "${ROOT}/src/system/lua_apps")
add_test(NAME manifest COMMAND manifest_test)
set_tests_properties(manifest PROPERTIES TIMEOUT 10)
add_executable(catalog_test catalog_test.cpp stubs/catalog_host.cpp
  "${ROOT}/src/system/lua_apps/sd_app_catalog.cpp"
  "${ROOT}/src/system/lua_apps/app_manifest.cpp")
target_include_directories(catalog_test PRIVATE "${CMAKE_CURRENT_LIST_DIR}/stubs"
  "${ROOT}/src/system/lua_apps")
add_test(NAME catalog COMMAND catalog_test)
set_tests_properties(catalog PROPERTIES TIMEOUT 10)
add_executable(app_check "${ROOT}/tools/lua-app-check.cpp"
  "${ROOT}/src/system/lua_apps/lua_runtime.cpp"
  "${ROOT}/src/system/lua_apps/app_manifest.cpp")
target_include_directories(app_check PRIVATE "${ROOT}/src/system/lua_apps")
target_link_libraries(app_check PRIVATE lua_test_engine)
# The native player replaces this optional SD package in upstream v0.9.0.
# If its directory exists, keep the test: an incomplete package must still fail.
if(EXISTS "${ROOT}/sd files/apps/mp3_player")
  add_executable(player_app_test "${ROOT}/test/media/player_app_test.cpp"
    "${ROOT}/src/system/lua_apps/lua_runtime.cpp"
    "${ROOT}/src/system/lua_apps/app_manifest.cpp")
  target_include_directories(player_app_test PRIVATE "${ROOT}/src/system/lua_apps"
    "${ROOT}/src/system/media")
  target_compile_definitions(player_app_test PRIVATE MEOW_TEST_ROOT="${ROOT}")
  target_link_libraries(player_app_test PRIVATE lua_test_engine)
  add_test(NAME player_app COMMAND player_app_test)
  set_tests_properties(player_app PROPERTIES TIMEOUT 15)
else()
  message(STATUS "Lua MP3 package not shipped; packaged-player test omitted")
endif()
add_subdirectory("${ROOT}/test/media" media)
add_subdirectory("${ROOT}/test/player_ui" player_ui)
add_subdirectory("${ROOT}/test/tracker_store" tracker_store)
add_subdirectory("${ROOT}/test/tracker_finder" tracker_finder)
