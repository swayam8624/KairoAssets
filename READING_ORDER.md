# KairoAssets: linked source reading order

Read identifiers, strong typed handles, registry and project manifests before importers or derived-data caches. KairoAssets owns portable asset identity, provenance and validation, while KairoRenderer creates transient GPU resources. KairoEngineCore persists asset references, so path and identity stability matter more than the current filesystem location.

## API interactions to understand

Follow AssetID -> validated manifest -> content fingerprint -> importer -> portable artifact -> renderer adapter. Study SHA-256, bounded path normalization, atomic replacement, cache invalidation and dependency cycle checks.

## Read these files in sequence (50)

1. [`KairoAssets.cppm`](KairoAssets.cppm) — inspect its imports, data ownership, validation and exported functions; follow its use from previous modules into downstream modules and tests.
2. [`AssetID.cppm`](AssetID.cppm) — inspect its imports, data ownership, validation and exported functions; follow its use from previous modules into downstream modules and tests.
3. [`AssetTypes.cppm`](AssetTypes.cppm) — inspect its imports, data ownership, validation and exported functions; follow its use from previous modules into downstream modules and tests.
4. [`AssetMetadata.cppm`](AssetMetadata.cppm) — inspect its imports, data ownership, validation and exported functions; follow its use from previous modules into downstream modules and tests.
5. [`AssetRegistry.cppm`](AssetRegistry.cppm) — inspect its imports, data ownership, validation and exported functions; follow its use from previous modules into downstream modules and tests.
6. [`AssetManifest.cppm`](AssetManifest.cppm) — inspect its imports, data ownership, validation and exported functions; follow its use from previous modules into downstream modules and tests.
7. [`AssetFingerprint.cppm`](AssetFingerprint.cppm) — inspect its imports, data ownership, validation and exported functions; follow its use from previous modules into downstream modules and tests.
8. [`AtomicFile.cppm`](AtomicFile.cppm) — inspect its imports, data ownership, validation and exported functions; follow its use from previous modules into downstream modules and tests.
9. [`DerivedDataCache.cppm`](DerivedDataCache.cppm) — inspect its imports, data ownership, validation and exported functions; follow its use from previous modules into downstream modules and tests.
10. [`MeshArtifact.cppm`](MeshArtifact.cppm) — inspect its imports, data ownership, validation and exported functions; follow its use from previous modules into downstream modules and tests.
11. [`TextureArtifact.cppm`](TextureArtifact.cppm) — inspect its imports, data ownership, validation and exported functions; follow its use from previous modules into downstream modules and tests.
12. [`BuiltinImporters.cppm`](BuiltinImporters.cppm) — inspect its imports, data ownership, validation and exported functions; follow its use from previous modules into downstream modules and tests.
13. [`AdvancedModelingOperations.cppm`](AdvancedModelingOperations.cppm) — inspect its imports, data ownership, validation and exported functions; follow its use from previous modules into downstream modules and tests.
14. [`AudioArtifact.cppm`](AudioArtifact.cppm) — inspect its imports, data ownership, validation and exported functions; follow its use from previous modules into downstream modules and tests.
15. [`BinaryFormat.cppm`](BinaryFormat.cppm) — inspect its imports, data ownership, validation and exported functions; follow its use from previous modules into downstream modules and tests.
16. [`DerivedArtifact.cppm`](DerivedArtifact.cppm) — inspect its imports, data ownership, validation and exported functions; follow its use from previous modules into downstream modules and tests.
17. [`EditableMesh.cppm`](EditableMesh.cppm) — inspect its imports, data ownership, validation and exported functions; follow its use from previous modules into downstream modules and tests.
18. [`EditableMeshDocument.cppm`](EditableMeshDocument.cppm) — inspect its imports, data ownership, validation and exported functions; follow its use from previous modules into downstream modules and tests.
19. [`GLBImporter.cppm`](GLBImporter.cppm) — inspect its imports, data ownership, validation and exported functions; follow its use from previous modules into downstream modules and tests.
20. [`GltfImporter.cppm`](GltfImporter.cppm) — inspect its imports, data ownership, validation and exported functions; follow its use from previous modules into downstream modules and tests.
21. [`GltfSceneArtifact.cppm`](GltfSceneArtifact.cppm) — inspect its imports, data ownership, validation and exported functions; follow its use from previous modules into downstream modules and tests.
22. [`ImportDatabase.cppm`](ImportDatabase.cppm) — inspect its imports, data ownership, validation and exported functions; follow its use from previous modules into downstream modules and tests.
23. [`Importer.cppm`](Importer.cppm) — inspect its imports, data ownership, validation and exported functions; follow its use from previous modules into downstream modules and tests.
24. [`ImporterRegistry.cppm`](ImporterRegistry.cppm) — inspect its imports, data ownership, validation and exported functions; follow its use from previous modules into downstream modules and tests.
25. [`MaterialArtifact.cppm`](MaterialArtifact.cppm) — inspect its imports, data ownership, validation and exported functions; follow its use from previous modules into downstream modules and tests.
26. [`MaterialAuthoring.cppm`](MaterialAuthoring.cppm) — inspect its imports, data ownership, validation and exported functions; follow its use from previous modules into downstream modules and tests.
27. [`ModelingOperations.cppm`](ModelingOperations.cppm) — inspect its imports, data ownership, validation and exported functions; follow its use from previous modules into downstream modules and tests.
28. [`OBJImporter.cppm`](OBJImporter.cppm) — inspect its imports, data ownership, validation and exported functions; follow its use from previous modules into downstream modules and tests.
29. [`PipelinePublish.cppm`](PipelinePublish.cppm) — inspect its imports, data ownership, validation and exported functions; follow its use from previous modules into downstream modules and tests.
30. [`Sculpting.cppm`](Sculpting.cppm) — inspect its imports, data ownership, validation and exported functions; follow its use from previous modules into downstream modules and tests.
31. [`SculptProduction.cppm`](SculptProduction.cppm) — inspect its imports, data ownership, validation and exported functions; follow its use from previous modules into downstream modules and tests.
32. [`SourceWatcher.cppm`](SourceWatcher.cppm) — inspect its imports, data ownership, validation and exported functions; follow its use from previous modules into downstream modules and tests.
33. [`tests/AdvancedModelingOperationsTests.cpp`](tests/AdvancedModelingOperationsTests.cpp) — inspect its imports, data ownership, validation and exported functions; follow its use from previous modules into downstream modules and tests.
34. [`tests/AssetStressTests.cpp`](tests/AssetStressTests.cpp) — inspect its imports, data ownership, validation and exported functions; follow its use from previous modules into downstream modules and tests.
35. [`tests/AssetTests.cpp`](tests/AssetTests.cpp) — inspect its imports, data ownership, validation and exported functions; follow its use from previous modules into downstream modules and tests.
36. [`tests/AudioArtifactTests.cpp`](tests/AudioArtifactTests.cpp) — inspect its imports, data ownership, validation and exported functions; follow its use from previous modules into downstream modules and tests.
37. [`tests/AuthoringCompletionTests.cpp`](tests/AuthoringCompletionTests.cpp) — inspect its imports, data ownership, validation and exported functions; follow its use from previous modules into downstream modules and tests.
38. [`tests/AuthoringDocumentTests.cpp`](tests/AuthoringDocumentTests.cpp) — inspect its imports, data ownership, validation and exported functions; follow its use from previous modules into downstream modules and tests.
39. [`tests/AuthoringMeshTests.cpp`](tests/AuthoringMeshTests.cpp) — inspect its imports, data ownership, validation and exported functions; follow its use from previous modules into downstream modules and tests.
40. [`tests/GLBImporterTests.cpp`](tests/GLBImporterTests.cpp) — inspect its imports, data ownership, validation and exported functions; follow its use from previous modules into downstream modules and tests.
41. [`tests/GltfAnimationImporterTests.cpp`](tests/GltfAnimationImporterTests.cpp) — inspect its imports, data ownership, validation and exported functions; follow its use from previous modules into downstream modules and tests.
42. [`tests/GltfSceneArtifactTests.cpp`](tests/GltfSceneArtifactTests.cpp) — inspect its imports, data ownership, validation and exported functions; follow its use from previous modules into downstream modules and tests.
43. [`tests/MaterialArtifactTests.cpp`](tests/MaterialArtifactTests.cpp) — inspect its imports, data ownership, validation and exported functions; follow its use from previous modules into downstream modules and tests.
44. [`tests/Phase2AssetTests.cpp`](tests/Phase2AssetTests.cpp) — inspect its imports, data ownership, validation and exported functions; follow its use from previous modules into downstream modules and tests.
45. [`tests/PipelinePublishTests.cpp`](tests/PipelinePublishTests.cpp) — inspect its imports, data ownership, validation and exported functions; follow its use from previous modules into downstream modules and tests.
46. [`tests/SculptProductionTests.cpp`](tests/SculptProductionTests.cpp) — inspect its imports, data ownership, validation and exported functions; follow its use from previous modules into downstream modules and tests.
47. [`tests/TextureArtifactTests.cpp`](tests/TextureArtifactTests.cpp) — inspect its imports, data ownership, validation and exported functions; follow its use from previous modules into downstream modules and tests.
48. [`TextureImporter.cppm`](TextureImporter.cppm) — inspect its imports, data ownership, validation and exported functions; follow its use from previous modules into downstream modules and tests.
49. [`UVAuthoring.cppm`](UVAuthoring.cppm) — inspect its imports, data ownership, validation and exported functions; follow its use from previous modules into downstream modules and tests.
50. [`WavImporter.cppm`](WavImporter.cppm) — inspect its imports, data ownership, validation and exported functions; follow its use from previous modules into downstream modules and tests.

## Verification and reuse

Read [README.md](README.md) and [CMakeLists.txt](CMakeLists.txt) for build/module boundaries where present. Test invalid inputs, lifetime errors, domain boundaries and the exact source revision being used by [KairoGameEngine](https://github.com/swayam8624/KairoGameEngine). A source file list or module compile is not full integration acceptance. Original software is shared under [MIT](LICENSE) for learning, reuse and continued development; external dependencies and assets retain their own terms. Per-function annotations are a subsequent documentation pass.
