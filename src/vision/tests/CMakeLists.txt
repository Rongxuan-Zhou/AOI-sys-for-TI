find_package(GTest REQUIRED)

add_executable(aoi_vision_tests
    test_smoke.cpp
    test_cse_locator.cpp
)
target_include_directories(aoi_vision_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(aoi_vision_tests PRIVATE aoi_vision_algo GTest::gtest GTest::gtest_main)
add_test(NAME aoi_vision_tests COMMAND aoi_vision_tests)
