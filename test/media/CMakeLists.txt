cmake_minimum_required(VERSION 3.20)
if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
  project(meow_media_tests LANGUAGES C CXX)
  enable_testing()
endif()
set(MEDIA_ROOT "${CMAKE_CURRENT_LIST_DIR}/../..")
add_executable(media_catalog_test media_catalog_test.cpp stubs/media_host.cpp
  "${MEDIA_ROOT}/src/system/media/media_catalog.cpp"
  "${MEDIA_ROOT}/src/system/media/media_paths.cpp")
target_compile_features(media_catalog_test PRIVATE cxx_std_17)
target_include_directories(media_catalog_test PRIVATE "${CMAKE_CURRENT_LIST_DIR}/stubs"
  "${MEDIA_ROOT}/src/system/media")
if(MSVC)
  target_compile_definitions(media_catalog_test PRIVATE _CRT_SECURE_NO_WARNINGS)
  target_compile_options(media_catalog_test PRIVATE /W4)
endif()
add_test(NAME media_catalog COMMAND media_catalog_test)
set_tests_properties(media_catalog PROPERTIES TIMEOUT 15)

add_executable(mp3_decode_test mp3_decode_test.cpp
  "${MEDIA_ROOT}/lib/ESP32-audioI2S/src/mp3_decoder/mp3_decoder.cpp")
target_compile_features(mp3_decode_test PRIVATE cxx_std_17)
target_include_directories(mp3_decode_test PRIVATE "${CMAKE_CURRENT_LIST_DIR}/helix_host"
  "${MEDIA_ROOT}/lib/ESP32-audioI2S/src"
  "${MEDIA_ROOT}/lib/ESP32-audioI2S/src/mp3_decoder")
set(MEOW_MEDIA_TEST_FIXTURE "${CMAKE_CURRENT_LIST_DIR}/fixtures/test-tone-44100.mp3" CACHE FILEPATH "Three-second stereo 44100Hz MP3 fixture for the real Helix decoder")
if(MEOW_MEDIA_TEST_FIXTURE)
  add_test(NAME mp3_decode COMMAND mp3_decode_test "${MEOW_MEDIA_TEST_FIXTURE}")
  set_tests_properties(mp3_decode PROPERTIES TIMEOUT 15)
endif()

add_executable(audio_dsp_test audio_dsp_test.cpp)
target_compile_features(audio_dsp_test PRIVATE cxx_std_17)
target_include_directories(audio_dsp_test PRIVATE "${MEDIA_ROOT}/lib/ESP32-audioI2S/src")
add_test(NAME audio_dsp COMMAND audio_dsp_test)

add_executable(dac_gain_test dac_gain_test.cpp)
target_compile_features(dac_gain_test PRIVATE cxx_std_17)
target_include_directories(dac_gain_test PRIVATE "${MEDIA_ROOT}/src/bsp/audio")
add_test(NAME dac_gain COMMAND dac_gain_test)

add_executable(audio_output_test audio_output_test.cpp)
target_compile_features(audio_output_test PRIVATE cxx_std_17)
target_include_directories(audio_output_test PRIVATE "${MEDIA_ROOT}/lib/ESP32-audioI2S/src")
add_test(NAME audio_output COMMAND audio_output_test)

add_executable(audio_equalizer_test audio_equalizer_test.cpp)
target_compile_features(audio_equalizer_test PRIVATE cxx_std_17)
target_include_directories(audio_equalizer_test PRIVATE "${MEDIA_ROOT}/src/system/media")
add_test(NAME audio_equalizer COMMAND audio_equalizer_test)

add_executable(audio_commands_test audio_commands_test.cpp)
target_compile_features(audio_commands_test PRIVATE cxx_std_17)
target_include_directories(audio_commands_test PRIVATE "${MEDIA_ROOT}/src/system/media")
add_test(NAME audio_commands COMMAND audio_commands_test)
set_tests_properties(audio_output audio_equalizer audio_commands PROPERTIES TIMEOUT 15)

add_executable(cover_test cover_test.cpp stubs/media_host.cpp
  "${MEDIA_ROOT}/src/system/media/cover_loader.cpp"
  "${MEDIA_ROOT}/src/system/media/media_paths.cpp"
  "${MEDIA_ROOT}/lib/LovyanGFX/src/lgfx/utility/lgfx_tjpgd.c")
target_compile_features(cover_test PRIVATE cxx_std_17)
target_include_directories(cover_test PRIVATE "${CMAKE_CURRENT_LIST_DIR}/stubs"
  "${MEDIA_ROOT}/src/system/media" "${MEDIA_ROOT}/lib/LovyanGFX/src")
add_test(NAME cover COMMAND cover_test "${CMAKE_CURRENT_LIST_DIR}/fixtures")
set_tests_properties(cover PROPERTIES TIMEOUT 20)
