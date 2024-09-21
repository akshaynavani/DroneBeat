include(GoogleTest)

add_executable(core_test core_test.cpp)
target_link_libraries(core_test PRIVATE dronebeat_core GTest::gtest_main)
gtest_discover_tests(core_test)

add_executable(storage_test storage_test.cpp)
target_link_libraries(storage_test PRIVATE dronebeat_storage GTest::gtest_main)
gtest_discover_tests(storage_test)

add_executable(monitor_test monitor_test.cpp)
target_link_libraries(monitor_test PRIVATE dronebeat_service GTest::gtest_main)
gtest_discover_tests(monitor_test)

add_executable(rest_test rest_test.cpp)
target_link_libraries(rest_test PRIVATE dronebeat_rest GTest::gtest_main)
gtest_discover_tests(rest_test)

add_executable(rpc_test rpc_test.cpp)
target_link_libraries(rpc_test PRIVATE dronebeat_rpc GTest::gtest_main)
gtest_discover_tests(rpc_test)

add_executable(sim_test sim_test.cpp)
target_link_libraries(sim_test PRIVATE dronebeat_sim dronebeat_service GTest::gtest_main)
gtest_discover_tests(sim_test)
