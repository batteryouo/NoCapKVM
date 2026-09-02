add_executable(test_common test_common.cpp)
target_link_libraries(test_common PRIVATE nockvm_common)
add_test(NAME test_common COMMAND test_common)

add_executable(test_discovery test_discovery.cpp)
target_link_libraries(test_discovery PRIVATE nockvm_discovery)
add_test(NAME test_discovery COMMAND test_discovery)

add_executable(test_crypto test_crypto.cpp)
target_link_libraries(test_crypto PRIVATE nockvm_discovery)
add_test(NAME test_crypto COMMAND test_crypto)

add_executable(test_topology test_topology.cpp)
target_link_libraries(test_topology PRIVATE nockvm_topology)
add_test(NAME test_topology COMMAND test_topology)

add_executable(test_audio test_audio.cpp)
target_link_libraries(test_audio PRIVATE nockvm_audio)
add_test(NAME test_audio COMMAND test_audio)
