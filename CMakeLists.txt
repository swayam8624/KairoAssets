cmake_minimum_required(VERSION 3.28)
project(KairoAssets VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(KAIRO_ASSETS_BUILD_TESTS "Build KairoAssets tests" ${PROJECT_IS_TOP_LEVEL})

add_library(KairoAssets)
target_sources(KairoAssets PUBLIC FILE_SET CXX_MODULES BASE_DIRS ${CMAKE_CURRENT_SOURCE_DIR} FILES
    AssetTypes.cppm
    AssetID.cppm
    AssetMetadata.cppm
    AssetRegistry.cppm
    KairoAssets.cppm)
if(CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
    target_compile_options(KairoAssets PRIVATE -Wall -Wextra -Wpedantic -Wconversion -Wshadow)
endif()

if(KAIRO_ASSETS_BUILD_TESTS)
    include(FetchContent)
    FetchContent_Declare(Catch2
        GIT_REPOSITORY https://github.com/catchorg/Catch2.git
        GIT_TAG v3.5.4)
    FetchContent_MakeAvailable(Catch2)

    add_executable(KairoAssetsTests tests/AssetTests.cpp)
    target_link_libraries(KairoAssetsTests PRIVATE KairoAssets Catch2::Catch2WithMain)
    enable_testing()
    add_test(NAME KairoAssetsTests COMMAND KairoAssetsTests)
endif()
