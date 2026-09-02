include(FetchContent)
FetchContent_Declare(miniaudio
  GIT_REPOSITORY https://github.com/mackron/miniaudio.git
  GIT_TAG 0.11.21
)
FetchContent_MakeAvailable(miniaudio)

# miniaudio ships no CMakeLists.txt of its own (it's a drop-in single
# header) -- wrap the fetched source in our own INTERFACE target, same
# situation src/app/CMakeLists.txt already handles for imgui.
add_library(miniaudio INTERFACE)
target_include_directories(miniaudio INTERFACE ${miniaudio_SOURCE_DIR})

add_library(nockvm_audio STATIC
  jitter_buffer.cpp
  protocol.cpp
  channel.cpp
  capture.cpp
  playback.cpp
  miniaudio_impl.cpp
)
target_include_directories(nockvm_audio PUBLIC ${CMAKE_SOURCE_DIR}/include)
target_link_libraries(nockvm_audio PUBLIC nockvm_common nockvm_discovery miniaudio)

if(NOT WIN32)
  # miniaudio dlopen()s the real PulseAudio/ALSA/etc libraries at runtime
  # rather than linking them directly -- pthread is already pulled in
  # transitively via nockvm_discovery's Threads::Threads.
  target_link_libraries(nockvm_audio PUBLIC dl m)
endif()
