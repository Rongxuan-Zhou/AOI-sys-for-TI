find_package(GTest REQUIRED)

add_executable(aoi_vision_tests
    test_smoke.cpp
    test_cse_locator.cpp
    test_defect_detector_routing.cpp
    test_ceramic_defects.cpp
    test_lighting_check.cpp
    test_top_side.cpp
    test_contamination.cpp
    test_side_view.cpp
)
target_include_directories(aoi_vision_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(aoi_vision_tests PRIVATE aoi_vision_algo GTest::gtest GTest::gtest_main)
add_test(NAME aoi_vision_tests COMMAND aoi_vision_tests)
