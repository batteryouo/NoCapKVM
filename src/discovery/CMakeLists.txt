find_package(Threads REQUIRED)

include(FetchContent)
set(SODIUM_DISABLE_TESTS ON)
FetchContent_Declare(Sodium
  GIT_REPOSITORY https://github.com/robinlinden/libsodium-cmake.git
  GIT_TAG 9b2848dfc1b917a9410f0de9d81059b26cbfaa8d
)
FetchContent_MakeAvailable(Sodium)

add_library(nockvm_discovery STATIC
  protocol.cpp
  identity.cpp
  platform_socket.cpp
  network_interfaces.cpp
  peer_table.cpp
  announcer.cpp
  listener.cpp
  tcp_server.cpp
  tcp_client.cpp
  noise_primitives.cpp
  noise_ik.cpp
  static_keys.cpp
  known_peers.cpp
  pairing.cpp
  secure_channel.cpp
  monitor_protocol.cpp
)
target_include_directories(nockvm_discovery PUBLIC ${CMAKE_SOURCE_DIR}/include)
target_link_libraries(nockvm_discovery PUBLIC nockvm_common nockvm_display Threads::Threads sodium)

if(WIN32)
  target_link_libraries(nockvm_discovery PUBLIC ws2_32 iphlpapi)
endif()
