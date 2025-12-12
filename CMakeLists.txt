cmake_minimum_required(VERSION 3.20)

project(tuirDefense LANGUAGES CXX)

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

add_executable(tuirDefense src/main.cpp src/audio.cpp)

target_link_libraries(tuirDefense PRIVATE ftxui::component ftxui::dom ftxui::screen)

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
  target_include_directories(tuirDefense PRIVATE ${MINIAUDIO_DIR})
  target_link_libraries(tuirDefense PRIVATE nlohmann_json::nlohmann_json)
  target_compile_definitions(tuirDefense PRIVATE ENABLE_AUDIO=1)
endif()

if(MSVC)
  target_compile_options(tuirDefense PRIVATE /W4 /permissive- /Zc:__cplusplus /WX)
else()
  target_compile_options(tuirDefense PRIVATE -Wall -Wextra -Wpedantic -Wconversion -Wsign-conversion -Werror)
  # Silence warnings from third-party headers we don't control.
  target_include_directories(tuirDefense SYSTEM PRIVATE
    ${CMAKE_BINARY_DIR}/miniaudio_header
    ${ftxui_SOURCE_DIR}/include)
endif()
