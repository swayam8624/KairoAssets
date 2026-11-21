# KairoAssets — source reading order

KairoAssets solves **durable asset identity and build-artifact management**, not GPU ownership, scene execution, or physics stepping. Its C++23 API is consumed by [KairoEngineCore](https://github.com/swayam8624/KairoEngineCore), [KairoRenderer](https://github.com/swayam8624/KairoRenderer), and the [KairoGameEngine player](https://github.com/swayam8624/KairoGameEngine/blob/main/Runtime/KairoPlayer/RuntimeProject.cppm).

## Foundations

1. [README](../README.md) and [CMakeLists](../CMakeLists.txt): supported features, external packages, tests and build flags.
2. [AssetTypes.cppm](../AssetTypes.cppm) and [AssetID.cppm](../AssetID.cppm): typed asset references and canonical persistent UUID identity. Persistent asset IDs survive path renames; GPU handles do not.
3. [AssetMetadata.cppm](../AssetMetadata.cppm): asset type, origin, importer version and dependency metadata.
4. [AssetRegistry.cppm](../AssetRegistry.cppm): concurrent metadata/identity registry, duplicate/path checks and dependency graph.
5. [AtomicFile.cppm](../AtomicFile.cppm) and [AssetManifest.cppm](../AssetManifest.cppm): project manifest format, load/save and validated project-relative path semantics.

## Import pipeline

6. [AssetFingerprint.cppm](../AssetFingerprint.cppm): content fingerprints for comparison/rebuild.
7. [ImportDatabase.cppm](../ImportDatabase.cppm): source import records and revision tracking.
8. [Importer.cppm](../Importer.cppm) and [ImporterRegistry.cppm](../ImporterRegistry.cppm): importer plugin contract and concrete importer lookup by identity/version.
9. [BuiltinImporters.cppm](../BuiltinImporters.cppm), [GLBImporter.cppm](../GLBImporter.cppm), [GltfImporter.cppm](../GltfImporter.cppm), [OBJImporter.cppm](../OBJImporter.cppm): input parsers and format-specific validation.
10. [DerivedArtifact.cppm](../DerivedArtifact.cppm), [DerivedDataCache.cppm](../DerivedDataCache.cppm), [BinaryFormat.cppm](../BinaryFormat.cppm): portable derived outputs, reproducible cache keys, and format contracts.
11. [MeshArtifact.cppm](../MeshArtifact.cppm), [MaterialArtifact.cppm](../MaterialArtifact.cppm), [TextureArtifact.cppm](../TextureArtifact.cppm), [AudioArtifact.cppm](../AudioArtifact.cppm): typed intermediate artifacts handed to consumers.

## Editing and publication

12. [EditableMesh.cppm](../EditableMesh.cppm) and [ModelingOperations.cppm](../ModelingOperations.cppm): editable data and modeling operations.
13. [MaterialAuthoring.cppm](../MaterialAuthoring.cppm) and [EditableMeshDocument.cppm](../EditableMeshDocument.cppm): persistent authoring representations distinct from renderer-owned GPU objects.
14. [PipelinePublish.cppm](../PipelinePublish.cppm): publishing artifacts and attestable source/derived paths to downstream tools.
15. [KairoAssets.cppm](../KairoAssets.cppm): top-level re-exported C++ module API, useful after understanding components.
16. [Tests](../tests/): use test cases to check path escaping, ID stability, malformed assets, importer selection, cache determinism and persistence.

## Trace the flow

**Import:** source bytes/path → validated `ImportRequest` → chosen importer → typed derived artifact → content-addressed cache and import records → manifest/registry identity → consuming Editor/Player.

**Render:** persistent `AssetID` → EngineCore scene reference → KairoGameEngine realtime scene bridge → KairoRenderer `AssetAdapters` → transient renderer handles. The asset database must not own GPU allocations.

**Authoring:** editable source document → modeling/authoring operations → validated artifact → publication/registry update → runtime rebuild. Cache invalidation must account for importer version, dependencies and source content.

## Exercises

1. Move an asset without changing its UUID; identify what manifest/path metadata changes.
2. Follow one `.glb` from bytes to mesh artifact and eventual renderer descriptor.
3. Explain why a source file timestamp alone is insufficient as a derived cache identity.
4. Find the checks that prevent project-relative paths from escaping their project root.
5. Test the difference between a material asset ID and a runtime texture handle.

The [MIT license](../LICENSE) covers original project code/documentation; third-party tooling, imported sample assets and separately licensed libraries retain their own terms.
