include(FetchContent)
FetchContent_Declare(stb
  GIT_REPOSITORY https://github.com/nothings/stb.git
  GIT_TAG 2c980bb59875b0d32144a71867fbdebb2f77cd20
)
FetchContent_MakeAvailable(stb)

# stb ships no CMakeLists.txt of its own (drop-in single headers) -- wrap
# the fetched source in our own INTERFACE target, same situation
# src/audio/CMakeLists.txt already handles for miniaudio.
add_library(stb INTERFACE)
target_include_directories(stb INTERFACE ${stb_SOURCE_DIR})

add_library(nockvm_clipboard STATIC
  jpeg_codec.cpp
  clipboard.cpp
  stb_impl.cpp
)
target_include_directories(nockvm_clipboard PUBLIC ${CMAKE_SOURCE_DIR}/include)
target_link_libraries(nockvm_clipboard PUBLIC nockvm_common stb)

if(WIN32)
  target_link_libraries(nockvm_clipboard PUBLIC user32)
else()
  find_package(X11 REQUIRED)
  target_include_directories(nockvm_clipboard PRIVATE ${X11_INCLUDE_DIR})
  target_link_libraries(nockvm_clipboard PUBLIC ${X11_LIBRARIES})
endif()
