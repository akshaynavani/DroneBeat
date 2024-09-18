include(GoogleTest)

add_executable(core_test core_test.cpp)
target_link_libraries(core_test PRIVATE dronebeat_core GTest::gtest_main)
gtest_discover_tests(core_test)

add_executable(storage_test storage_test.cpp)
target_link_libraries(storage_test PRIVATE dronebeat_storage GTest::gtest_main)
gtest_discover_tests(storage_test)
