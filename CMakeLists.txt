cmake_minimum_required(VERSION 3.20)

project(tuirDefense LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

include(FetchContent)

FetchContent_Declare(
  ftxui
  GIT_REPOSITORY https://github.com/ArthurSonzogni/ftxui.git
  GIT_TAG v5.0.0
  GIT_SHALLOW TRUE
)

FetchContent_MakeAvailable(ftxui)

add_executable(tuirDefense src/main.cpp)

target_link_libraries(tuirDefense PRIVATE ftxui::component ftxui::dom ftxui::screen)

if(MSVC)
  target_compile_options(tuirDefense PRIVATE /W4 /permissive- /Zc:__cplusplus)
else()
  target_compile_options(tuirDefense PRIVATE -Wall -Wextra -Wpedantic -Wconversion -Wsign-conversion)
endif()
