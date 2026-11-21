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
