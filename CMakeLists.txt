cmake_minimum_required(VERSION 3.20)

project(catcat LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

include(FetchContent)

option(ENABLE_AUDIO "Enable audio (miniaudio)" ON)

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

add_executable(catcat src/main.cpp src/audio.cpp)

target_link_libraries(catcat PRIVATE ftxui::component ftxui::dom ftxui::screen)

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
  target_include_directories(catcat PRIVATE ${MINIAUDIO_DIR})
  target_link_libraries(catcat PRIVATE nlohmann_json::nlohmann_json)
  target_compile_definitions(catcat PRIVATE ENABLE_AUDIO=1)
endif()

if(MSVC)
  target_compile_options(catcat PRIVATE /W4 /permissive- /Zc:__cplusplus /WX)
else()
  target_compile_options(catcat PRIVATE -Wall -Wextra -Wpedantic -Wconversion -Wsign-conversion -Werror)
  # Silence warnings from third-party headers we don't control.
  target_include_directories(catcat SYSTEM PRIVATE
    ${CMAKE_BINARY_DIR}/miniaudio_header
    ${ftxui_SOURCE_DIR}/include)
endif()

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
  DEPENDS catcat ${CMAKE_SOURCE_DIR}/audio.json ${CATCAT_AUDIO_FILES}
  BYPRODUCTS ${CMAKE_BINARY_DIR}/catcat_bundle.zip
  COMMENT "Creating catcat_bundle.zip with executable and audio assets"
  VERBATIM
)

add_custom_target(package_catcat ALL DEPENDS ${CMAKE_BINARY_DIR}/catcat_bundle.zip)
