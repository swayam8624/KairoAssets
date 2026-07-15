cmake_minimum_required(VERSION 3.28)
project(KairoAssets VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(KAIRO_ASSETS_BUILD_TESTS "Build KairoAssets tests" ${PROJECT_IS_TOP_LEVEL})

if(CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
    add_compile_options(-Wall -Wextra -Wpedantic -Wconversion -Wshadow)
endif()

add_library(KairoAssets)
target_sources(KairoAssets PUBLIC FILE_SET CXX_MODULES BASE_DIRS ${CMAKE_CURRENT_SOURCE_DIR} FILES
    AssetTypes.cppm
    AssetID.cppm
    KairoAssets.cppm)

if(KAIRO_ASSETS_BUILD_TESTS)
    include(FetchContent)
    FetchContent_Declare(Catch2
        GIT_REPOSITORY https://github.com/catchorg/Catch2.git
        GIT_TAG v3.5.4)
    FetchContent_MakeAvailable(Catch2)

    if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/tests/AssetTests.cpp")
        add_executable(KairoAssetsTests tests/AssetTests.cpp)
        target_link_libraries(KairoAssetsTests PRIVATE KairoAssets Catch2::Catch2WithMain)
        enable_testing()
        add_test(NAME KairoAssetsTests COMMAND KairoAssetsTests)
    endif()
endif()
