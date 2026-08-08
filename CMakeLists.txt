cmake_minimum_required(VERSION 3.28)
project(KairoAssets VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(KAIRO_ASSETS_BUILD_TESTS "Build KairoAssets tests" ${PROJECT_IS_TOP_LEVEL})

include(FetchContent)
FetchContent_Declare(KairoAssetsStb
    GIT_REPOSITORY https://github.com/nothings/stb.git
    GIT_TAG 2c980bb59875b0d32144a71867fbdebb2f77cd20)
FetchContent_Declare(KairoAssetsCgltf
    GIT_REPOSITORY https://github.com/jkuhlmann/cgltf.git
    GIT_TAG 85cd62382dfea638278962690cf515023f33ed00)
if(NOT TARGET nlohmann_json::nlohmann_json)
    FetchContent_Declare(nlohmann_json
        GIT_REPOSITORY https://github.com/nlohmann/json.git
        GIT_TAG v3.11.3
        GIT_SHALLOW TRUE)
endif()
FetchContent_MakeAvailable(KairoAssetsStb KairoAssetsCgltf nlohmann_json)

add_library(KairoAssets)
target_sources(KairoAssets PUBLIC FILE_SET CXX_MODULES BASE_DIRS ${CMAKE_CURRENT_SOURCE_DIR} FILES
    AssetTypes.cppm
    AssetID.cppm
    AtomicFile.cppm
    AssetFingerprint.cppm
    ImportDatabase.cppm
    DerivedDataCache.cppm
    BinaryFormat.cppm
    DerivedArtifact.cppm
    MeshArtifact.cppm
    TextureArtifact.cppm
    MaterialArtifact.cppm
    GltfSceneArtifact.cppm
    SourceWatcher.cppm
    Importer.cppm
    ImporterRegistry.cppm
    OBJImporter.cppm
    TextureImporter.cppm
    GltfImporter.cppm
    GLBImporter.cppm
    BuiltinImporters.cppm
    AssetMetadata.cppm
    AssetRegistry.cppm
    AssetManifest.cppm
    KairoAssets.cppm)
target_include_directories(KairoAssets SYSTEM PRIVATE
    ${kairoassetsstb_SOURCE_DIR}
    ${kairoassetscgltf_SOURCE_DIR})
target_link_libraries(KairoAssets PRIVATE nlohmann_json::nlohmann_json)
if(CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
    target_compile_options(KairoAssets PRIVATE -Wall -Wextra -Wpedantic -Wconversion -Wshadow)
endif()

if(KAIRO_ASSETS_BUILD_TESTS)
    include(FetchContent)
    FetchContent_Declare(Catch2
        GIT_REPOSITORY https://github.com/catchorg/Catch2.git
        GIT_TAG v3.5.4)
    FetchContent_MakeAvailable(Catch2)

    add_executable(KairoAssetsTests
        tests/AssetTests.cpp
        tests/TextureArtifactTests.cpp
        tests/MaterialArtifactTests.cpp
        tests/GLBImporterTests.cpp)
    target_link_libraries(KairoAssetsTests PRIVATE
        KairoAssets Catch2::Catch2WithMain nlohmann_json::nlohmann_json)
    add_executable(KairoAssetsPhase2Tests tests/Phase2AssetTests.cpp)
    target_link_libraries(KairoAssetsPhase2Tests PRIVATE KairoAssets Catch2::Catch2WithMain)
    enable_testing()
    add_test(NAME KairoAssetsTests COMMAND KairoAssetsTests)
    add_test(NAME KairoAssetsPhase2Tests COMMAND KairoAssetsPhase2Tests)
endif()
