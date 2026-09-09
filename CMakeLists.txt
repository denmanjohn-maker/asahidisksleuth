cmake_minimum_required(VERSION 3.28)

project(asahidisksleuth
    VERSION 0.1.0
    DESCRIPTION "Honest disk usage analysis for Linux"
    LANGUAGES CXX
)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()

option(ADS_BUILD_CLI "Build the asahidisksleuth CLI" ON)
option(ADS_BUILD_APP "Build the Kirigami GUI app" ON)
option(ADS_BUILD_TESTS "Build tests" ON)

find_package(Qt6 6.5 REQUIRED COMPONENTS Core)
qt_standard_project_setup()

add_subdirectory(src/core)

if(ADS_BUILD_CLI)
    add_subdirectory(src/cli)
endif()

if(ADS_BUILD_APP)
    find_package(Qt6 6.5 REQUIRED COMPONENTS Quick Qml)
    add_subdirectory(src/app)
endif()

if(ADS_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()
