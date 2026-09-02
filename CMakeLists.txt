cmake_minimum_required(VERSION 3.20)
project(NoCapKVM LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(MSVC)
  add_compile_options(/W4)
else()
  add_compile_options(-Wall -Wextra)
endif()

add_subdirectory(src/common)
add_subdirectory(src/display)
add_subdirectory(src/topology)
add_subdirectory(src/discovery)
add_subdirectory(src/app)

enable_testing()
add_subdirectory(tests)
