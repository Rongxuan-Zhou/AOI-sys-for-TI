cmake_minimum_required(VERSION 3.16)
project(aoi_vision_core LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

option(AOI_WITH_MVS    "Build GigE capture + Python module against the Hikrobot MVS SDK" ON)
option(AOI_BUILD_TESTS "Build GoogleTest unit tests (synthetic images, no hardware)" OFF)

find_package(OpenCV 4.0 REQUIRED COMPONENTS core imgproc imgcodecs)

# --- SDK-free algorithm library ---------------------------------------------
# Everything a detector needs: disc geometry, preprocessing, lighting check,
# defect detection. Compiles and tests without the camera SDK.
add_library(aoi_vision_algo STATIC
    CseLocator.cpp
    ImagePreprocessor.cpp
    LightingCheck.cpp
    DefectDetector.cpp
)
target_include_directories(aoi_vision_algo PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${OpenCV_INCLUDE_DIRS}
)
target_link_libraries(aoi_vision_algo PUBLIC ${OpenCV_LIBS})
target_compile_options(aoi_vision_algo PRIVATE -Wall -Wextra)

# --- Production core (needs the Hikrobot MVS SDK) ----------------------------
if(AOI_WITH_MVS)
    find_package(pybind11 REQUIRED)

    set(MVS_SDK_DIR "/opt/MVS" CACHE PATH "Hikrobot MVS SDK root directory")
    find_library(MVS_LIB MvCameraControl
        PATHS ${MVS_SDK_DIR}/lib/64 ${MVS_SDK_DIR}/lib/aarch64
        REQUIRED
    )

    add_library(aoi_vision_core SHARED GigEVisionCapture.cpp)
    target_include_directories(aoi_vision_core PUBLIC ${MVS_SDK_DIR}/include)
    target_link_libraries(aoi_vision_core PUBLIC aoi_vision_algo ${MVS_LIB} pthread)

    pybind11_add_module(_aoi_vision_core pybind_module.cpp)
    target_link_libraries(_aoi_vision_core PRIVATE aoi_vision_core)
endif()

if(AOI_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()
