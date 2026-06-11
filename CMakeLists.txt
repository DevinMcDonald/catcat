cmake_minimum_required(VERSION 3.20)
project(catcat LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Derive version from git if available; fallback to 0.1.0.
execute_process(
  COMMAND git describe --tags --dirty --always
  WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
  OUTPUT_VARIABLE GIT_DESC
  OUTPUT_STRIP_TRAILING_WHITESPACE
  RESULT_VARIABLE GIT_DESC_RES
)
if (NOT GIT_DESC_RES EQUAL 0 OR GIT_DESC STREQUAL "")
  set(GIT_DESC "0.1.0")
endif()
set(CATCAT_VERSION "${GIT_DESC}")

include(FetchContent)
option(ENABLE_AUDIO "Enable audio (miniaudio)" ON)
option(BUILD_TESTS "Build GoogleTest unit tests" ON)

file(MAKE_DIRECTORY ${CMAKE_BINARY_DIR}/generated/version)
configure_file(
  ${CMAKE_SOURCE_DIR}/src/version/version.h.in
  ${CMAKE_BINARY_DIR}/generated/version/version.h
  @ONLY
)

FetchContent_Declare(
  ftxui
  GIT_REPOSITORY https://github.com/ArthurSonzogni/ftxui.git
  GIT_TAG v5.0.0
  GIT_SHALLOW TRUE
)
FetchContent_MakeAvailable(ftxui)

if(ENABLE_AUDIO)
  FetchContent_Declare(
    nlohmann_json
    GIT_REPOSITORY https://github.com/nlohmann/json.git
    GIT_TAG v3.11.3
    GIT_SHALLOW TRUE
  )
  FetchContent_MakeAvailable(nlohmann_json)
endif()

if(BUILD_TESTS)
  FetchContent_Declare(
    googletest
    GIT_REPOSITORY https://github.com/google/googletest.git
    GIT_TAG        v1.14.0
    GIT_SHALLOW    TRUE
  )
  set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
  FetchContent_MakeAvailable(googletest)
endif()

# ── Shared game logic library (used by both catcat and tests) ────────────────
add_library(catcat_lib STATIC
  src/game/game.cpp
  src/audio/audio.cpp
  src/version/version.cpp
  src/version/update_checker.cpp
)
target_link_libraries(catcat_lib PUBLIC ftxui::component ftxui::dom ftxui::screen)
target_include_directories(catcat_lib PUBLIC
  ${CMAKE_SOURCE_DIR}/src
  ${CMAKE_BINARY_DIR}/generated
)

if(ENABLE_AUDIO)
  set(MINIAUDIO_DIR ${CMAKE_BINARY_DIR}/miniaudio_header)
  file(MAKE_DIRECTORY ${MINIAUDIO_DIR})
  set(MINIAUDIO_HEADER ${MINIAUDIO_DIR}/miniaudio.h)
  if(NOT EXISTS ${MINIAUDIO_HEADER})
    file(DOWNLOAD
      https://raw.githubusercontent.com/mackron/miniaudio/master/miniaudio.h
      ${MINIAUDIO_HEADER}
      STATUS DL_MINIAUDIO
      SHOW_PROGRESS
    )
  endif()
  target_include_directories(catcat_lib PUBLIC ${MINIAUDIO_DIR})
  target_link_libraries(catcat_lib PUBLIC nlohmann_json::nlohmann_json)
  target_compile_definitions(catcat_lib PUBLIC ENABLE_AUDIO=1)
endif()

if(MSVC)
  target_compile_options(catcat_lib PRIVATE /W4 /permissive- /Zc:__cplusplus /WX)
else()
    target_compile_options(catcat_lib PRIVATE
        -Wall -Wextra -Wpedantic -Wconversion -Wsign-conversion -Werror
        -Wno-unused-variable -Wno-unused-parameter)
  target_include_directories(catcat_lib SYSTEM PRIVATE
    ${CMAKE_BINARY_DIR}/miniaudio_header
    ${ftxui_SOURCE_DIR}/include)
endif()

# ── Main executable ──────────────────────────────────────────────────────────
add_executable(catcat src/main.cpp)
target_link_libraries(catcat PRIVATE catcat_lib)

# ── Tests ────────────────────────────────────────────────────────────────────
enable_testing()
add_test(NAME smoke_test COMMAND catcat --version)

if(BUILD_TESTS)
  include(GoogleTest)

  add_executable(catcat_tests
    test/test_example.cpp
  )
  target_link_libraries(catcat_tests PRIVATE catcat_lib GTest::gtest_main)
  target_include_directories(catcat_tests PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_BINARY_DIR}/generated
  )
  if(NOT MSVC)
    target_include_directories(catcat_tests SYSTEM PRIVATE
      ${ftxui_SOURCE_DIR}/include)
  endif()

  gtest_discover_tests(catcat_tests)
endif()

# ── Packaging ────────────────────────────────────────────────────────────────
set(CATCAT_PACKAGE_DIR ${CMAKE_BINARY_DIR}/catcat_package)
file(GLOB_RECURSE CATCAT_AUDIO_FILES CONFIGURE_DEPENDS ${CMAKE_SOURCE_DIR}/audio/*)
add_custom_command(
  OUTPUT ${CMAKE_BINARY_DIR}/catcat_bundle.zip
  COMMAND ${CMAKE_COMMAND} -E rm -rf ${CATCAT_PACKAGE_DIR}
  COMMAND ${CMAKE_COMMAND} -E make_directory ${CATCAT_PACKAGE_DIR}
  COMMAND ${CMAKE_COMMAND} -E copy $<TARGET_FILE:catcat> ${CATCAT_PACKAGE_DIR}/
  COMMAND ${CMAKE_COMMAND} -E copy ${CMAKE_SOURCE_DIR}/audio.json ${CATCAT_PACKAGE_DIR}/
  COMMAND ${CMAKE_COMMAND} -E copy_directory ${CMAKE_SOURCE_DIR}/audio ${CATCAT_PACKAGE_DIR}/audio
  COMMAND ${CMAKE_COMMAND} -E chdir ${CATCAT_PACKAGE_DIR}
    ${CMAKE_COMMAND} -E tar cf ${CMAKE_BINARY_DIR}/catcat_bundle.zip --format=zip -- .
  COMMAND ${CMAKE_COMMAND} -E echo "catcat_bundle.zip SHA256:"
  COMMAND ${CMAKE_COMMAND} -E sha256sum ${CMAKE_BINARY_DIR}/catcat_bundle.zip
  DEPENDS catcat ${CMAKE_SOURCE_DIR}/audio.json ${CATCAT_AUDIO_FILES}
  COMMENT "Creating catcat_bundle.zip with executable and audio assets"
  VERBATIM
)
add_custom_target(package_catcat ALL DEPENDS ${CMAKE_BINARY_DIR}/catcat_bundle.zip)
