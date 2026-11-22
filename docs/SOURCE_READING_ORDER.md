# Teaching source map — KairoAssets

Canonical asset identities, manifests, strict importers and derived caches.

Read these source links in order, identify the caller and callee for each public API, then check the error/ownership contract and its tests.

## Reading order

Read the [complete asset-pipeline walkthrough](ASSET_PIPELINE_WALKTHROUGH.md)
alongside these files to understand stable identity, transactional metadata,
cache lifetime and GPU upload boundaries.

### Part A — Portable identity and safe registry operations

1. [`CMakeLists.txt`](../CMakeLists.txt) and [`KairoAssets.cppm`](../KairoAssets.cppm): C++23 build and exported API surface.
2. [`AssetID.cppm`](../AssetID.cppm) and [`AssetTypes.cppm`](../AssetTypes.cppm): stable UUID and typed handles.
3. [`AssetMetadata.cppm`](../AssetMetadata.cppm): source origins, revisions, path and dependency rules.
4. [`AssetRegistry.cppm`](../AssetRegistry.cppm): locks, detached metadata, dependency validation and transactional ReplaceAll.
5. [`AssetManifest.cppm`](../AssetManifest.cppm) and [`AtomicFile.cppm`](../AtomicFile.cppm): deterministic text, parsing, temporary-file replacement.
6. [`tests/AssetTests.cpp`](../tests/AssetTests.cpp): identity/path/dependency behavior.

**Exercise:** create a material dependent on a texture; move the texture,
observe that the typed ID remains stable, and verify removal is rejected
until the dependent reference is changed. Try malformed manifest loading
and confirm there is no partial registry replacement.

### Part B — Content hashes, provenance and derived artifacts

7. [`AssetFingerprint.cppm`](../AssetFingerprint.cppm): SHA-256 content identity.
8. [`ImportDatabase.cppm`](../ImportDatabase.cppm): source/importer-version/settings provenance.
9. [`DerivedDataCache.cppm`](../DerivedDataCache.cppm): immutable derived key, staged publishing and concurrent writers.
10. [`BinaryFormat.cppm`](../BinaryFormat.cppm) and [`DerivedArtifact.cppm`](../DerivedArtifact.cppm): serialized typed artifact envelope.
11. [`MeshArtifact.cppm`](../MeshArtifact.cppm), [`TextureArtifact.cppm`](../TextureArtifact.cppm), [`MaterialArtifact.cppm`](../MaterialArtifact.cppm) and [`AudioArtifact.cppm`](../AudioArtifact.cppm): portable runtime data.
12. [`tests/Phase2AssetTests.cpp`](../tests/Phase2AssetTests.cpp) and [`tests/AssetStressTests.cpp`](../tests/AssetStressTests.cpp): provenance, cache and stress cases.

**Exercise:** modify only the importer version and recompute a derived
key. A source digest alone is insufficient for a reproducible artifact
cache; record the entire canonical recipe.

### Part C — Importers and scene publication

13. [`Importer.cppm`](../Importer.cppm), [`ImporterRegistry.cppm`](../ImporterRegistry.cppm) and [`BuiltinImporters.cppm`](../BuiltinImporters.cppm): importer contracts and version selection.
14. [`OBJImporter.cppm`](../OBJImporter.cppm), [`TextureImporter.cppm`](../TextureImporter.cppm) and [`WavImporter.cppm`](../WavImporter.cppm): source decoders.
15. [`GltfImporter.cppm`](../GltfImporter.cppm) and [`GLBImporter.cppm`](../GLBImporter.cppm): external URI/path safety, accessors and transforms.
16. [`GltfSceneArtifact.cppm`](../GltfSceneArtifact.cppm): portable scene nodes and primitive/material references.
17. [`PipelinePublish.cppm`](../PipelinePublish.cppm): strict DCC bundle fingerprint and registration boundary.
18. [`SourceWatcher.cppm`](../SourceWatcher.cppm): explicit file-observation service.
19. [`tests/GLBImporterTests.cpp`](../tests/GLBImporterTests.cpp), [`tests/GltfSceneArtifactTests.cpp`](../tests/GltfSceneArtifactTests.cpp) and [`tests/PipelinePublishTests.cpp`](../tests/PipelinePublishTests.cpp): importer/publish validation.

**Exercise:** follow a glTF material texture from a safe project-local URI,
through an importer and artifact, to
[KairoRenderer AssetAdapters](https://github.com/swayam8624/KairoRenderer/blob/docs/teaching-walkthrough-20261121/AssetAdapters.cppm).
Locate where persistent AssetID becomes a renderer-owned TextureHandle.

### Part D — Editable content versus generated artifacts

20. [`EditableMesh.cppm`](../EditableMesh.cppm) and [`EditableMeshDocument.cppm`](../EditableMeshDocument.cppm): editable topology and document state.
21. [`ModelingOperations.cppm`](../ModelingOperations.cppm), [`AdvancedModelingOperations.cppm`](../AdvancedModelingOperations.cppm) and [`UVAuthoring.cppm`](../UVAuthoring.cppm): geometry/UV authoring contracts.
22. [`MaterialAuthoring.cppm`](../MaterialAuthoring.cppm), [`Sculpting.cppm`](../Sculpting.cppm) and [`SculptProduction.cppm`](../SculptProduction.cppm): materials and sculpting workflows.
23. [`tests/AuthoringMeshTests.cpp`](../tests/AuthoringMeshTests.cpp), [`tests/AuthoringDocumentTests.cpp`](../tests/AuthoringDocumentTests.cpp) and [`tests/SculptProductionTests.cpp`](../tests/SculptProductionTests.cpp): topology/operation tests.
24. Continue through all remaining first-party tests and APIs in the complete inventory below.

**Exercise:** distinguish an editable mesh with authoring history from
the validated baked mesh artifact handed to the renderer. Preservation
of an authored source is not the same as preserving a derived cache entry.

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
