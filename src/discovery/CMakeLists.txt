find_package(Threads REQUIRED)

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
)
target_include_directories(nockvm_discovery PUBLIC ${CMAKE_SOURCE_DIR}/include)
target_link_libraries(nockvm_discovery PUBLIC nockvm_common Threads::Threads)

if(WIN32)
  target_link_libraries(nockvm_discovery PUBLIC ws2_32 iphlpapi)
endif()
