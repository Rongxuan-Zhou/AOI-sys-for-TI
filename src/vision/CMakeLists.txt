cmake_minimum_required(VERSION 3.16)
project(aoi_vision_core LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

# --- Dependencies -----------------------------------------------------------
find_package(OpenCV 4.0 REQUIRED COMPONENTS core imgproc highgui calib3d)
find_package(pybind11 REQUIRED)

# Hikrobot MVS SDK — install path follows MVS default layout
set(MVS_SDK_DIR "/opt/MVS" CACHE PATH "Hikrobot MVS SDK root directory")
find_library(MVS_LIB MvCameraControl
    PATHS ${MVS_SDK_DIR}/lib/64 ${MVS_SDK_DIR}/lib/aarch64
    REQUIRED
)
set(MVS_INCLUDE_DIR ${MVS_SDK_DIR}/include)

# --- Core shared library ----------------------------------------------------
add_library(aoi_vision_core SHARED
    GigEVisionCapture.cpp
    ImagePreprocessor.cpp
    DefectDetector.cpp
)

target_include_directories(aoi_vision_core PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${MVS_INCLUDE_DIR}
    ${OpenCV_INCLUDE_DIRS}
)

target_link_libraries(aoi_vision_core PUBLIC
    ${MVS_LIB}
    ${OpenCV_LIBS}
    pthread
)

# --- pybind11 Python module --------------------------------------------------
pybind11_add_module(_aoi_vision_core pybind_module.cpp)
target_link_libraries(_aoi_vision_core PRIVATE aoi_vision_core)
