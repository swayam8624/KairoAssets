# Teaching source map — KairoAssets

Canonical asset identities, manifests, strict importers and derived caches.

Read these source links in order, identify the caller and callee for each public API, then check the error/ownership contract and its tests.

## Reading order

1. [`CMakeLists.txt`](../CMakeLists.txt)
2. [`KairoAssets.cppm`](../KairoAssets.cppm)
3. [`AssetID.cppm`](../AssetID.cppm)
4. [`AssetTypes.cppm`](../AssetTypes.cppm)
5. [`AssetMetadata.cppm`](../AssetMetadata.cppm)
6. [`AssetRegistry.cppm`](../AssetRegistry.cppm)
7. [`AssetManifest.cppm`](../AssetManifest.cppm)
8. [`Importer.cppm`](../Importer.cppm)
9. [`ImporterRegistry.cppm`](../ImporterRegistry.cppm)
10. [`OBJImporter.cppm`](../OBJImporter.cppm)
11. [`GltfImporter.cppm`](../GltfImporter.cppm)
12. [`GLBImporter.cppm`](../GLBImporter.cppm)
13. [`MeshArtifact.cppm`](../MeshArtifact.cppm)
14. [`TextureArtifact.cppm`](../TextureArtifact.cppm)
15. [`DerivedDataCache.cppm`](../DerivedDataCache.cppm)
16. [`PipelinePublish.cppm`](../PipelinePublish.cppm)
17. [`tests`](../tests)

## Repository integration and API contracts

Editor and DCC publish source assets; KairoAssets validates identity/hash/dependencies and emits portable mesh/material/texture artifacts; Renderer and EngineCore use them through stable IDs.

## Teaching standards

- Comments explain invariants, decision reasons, lifetime, prerequisites and cross-file effects; they do not narrate obvious statements.
- Follow upstream interfaces before implementation details; distinguish implemented, host-verified and planned features.
- Run available tests at a pinned commit and consult [STATUS.md](../STATUS.md) / [STATUS.yaml](../STATUS.yaml) before declaring completion.
- Reuse original contributions under [LICENSE](../LICENSE), subject to third-party code and asset terms.


## Complete source-file inventory

After the curated teaching path, follow this deterministic inventory of first-party source, scripts, shaders, and tests. Grouping is a suggested learning order, not an indication that every function is already annotated. Vendored dependencies, ROMs, binaries, data, media, and generated evidence are excluded.

### Entry points and build (1)

1. [`CMakeLists.txt`](../CMakeLists.txt)

### Interfaces and modules (35)

1. [`AdvancedModelingOperations.cppm`](../AdvancedModelingOperations.cppm)
2. [`AssetFingerprint.cppm`](../AssetFingerprint.cppm)
3. [`AssetID.cppm`](../AssetID.cppm)
4. [`AssetManifest.cppm`](../AssetManifest.cppm)
5. [`AssetMetadata.cppm`](../AssetMetadata.cppm)
6. [`AssetRegistry.cppm`](../AssetRegistry.cppm)
7. [`AssetTypes.cppm`](../AssetTypes.cppm)
8. [`AtomicFile.cppm`](../AtomicFile.cppm)
9. [`AudioArtifact.cppm`](../AudioArtifact.cppm)
10. [`BinaryFormat.cppm`](../BinaryFormat.cppm)
11. [`BuiltinImporters.cppm`](../BuiltinImporters.cppm)
12. [`DerivedArtifact.cppm`](../DerivedArtifact.cppm)
13. [`DerivedDataCache.cppm`](../DerivedDataCache.cppm)
14. [`EditableMesh.cppm`](../EditableMesh.cppm)
15. [`EditableMeshDocument.cppm`](../EditableMeshDocument.cppm)
16. [`GLBImporter.cppm`](../GLBImporter.cppm)
17. [`GltfImporter.cppm`](../GltfImporter.cppm)
18. [`GltfSceneArtifact.cppm`](../GltfSceneArtifact.cppm)
19. [`ImportDatabase.cppm`](../ImportDatabase.cppm)
20. [`Importer.cppm`](../Importer.cppm)
21. [`ImporterRegistry.cppm`](../ImporterRegistry.cppm)
22. [`KairoAssets.cppm`](../KairoAssets.cppm)
23. [`MaterialArtifact.cppm`](../MaterialArtifact.cppm)
24. [`MaterialAuthoring.cppm`](../MaterialAuthoring.cppm)
25. [`MeshArtifact.cppm`](../MeshArtifact.cppm)
26. [`ModelingOperations.cppm`](../ModelingOperations.cppm)
27. [`OBJImporter.cppm`](../OBJImporter.cppm)
28. [`PipelinePublish.cppm`](../PipelinePublish.cppm)
29. [`Sculpting.cppm`](../Sculpting.cppm)
30. [`SculptProduction.cppm`](../SculptProduction.cppm)
31. [`SourceWatcher.cppm`](../SourceWatcher.cppm)
32. [`TextureArtifact.cppm`](../TextureArtifact.cppm)
33. [`TextureImporter.cppm`](../TextureImporter.cppm)
34. [`UVAuthoring.cppm`](../UVAuthoring.cppm)
35. [`WavImporter.cppm`](../WavImporter.cppm)

### Verification and tests (15)

1. [`tests/AdvancedModelingOperationsTests.cpp`](../tests/AdvancedModelingOperationsTests.cpp)
2. [`tests/AssetStressTests.cpp`](../tests/AssetStressTests.cpp)
3. [`tests/AssetTests.cpp`](../tests/AssetTests.cpp)
4. [`tests/AudioArtifactTests.cpp`](../tests/AudioArtifactTests.cpp)
5. [`tests/AuthoringCompletionTests.cpp`](../tests/AuthoringCompletionTests.cpp)
6. [`tests/AuthoringDocumentTests.cpp`](../tests/AuthoringDocumentTests.cpp)
7. [`tests/AuthoringMeshTests.cpp`](../tests/AuthoringMeshTests.cpp)
8. [`tests/GLBImporterTests.cpp`](../tests/GLBImporterTests.cpp)
9. [`tests/GltfAnimationImporterTests.cpp`](../tests/GltfAnimationImporterTests.cpp)
10. [`tests/GltfSceneArtifactTests.cpp`](../tests/GltfSceneArtifactTests.cpp)
11. [`tests/MaterialArtifactTests.cpp`](../tests/MaterialArtifactTests.cpp)
12. [`tests/Phase2AssetTests.cpp`](../tests/Phase2AssetTests.cpp)
13. [`tests/PipelinePublishTests.cpp`](../tests/PipelinePublishTests.cpp)
14. [`tests/SculptProductionTests.cpp`](../tests/SculptProductionTests.cpp)
15. [`tests/TextureArtifactTests.cpp`](../tests/TextureArtifactTests.cpp)

Snapshot totals: 51 first-party source/build/test files, 63 tracked files overall. Re-audit when files are renamed or added.
