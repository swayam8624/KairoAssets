# From source file to stable asset reference and renderer upload

This source-linked guide explains how KairoAssets manages portable identity,
safe imports, immutable derived artifacts and authoring. It complements the
[complete source reading order](SOURCE_READING_ORDER.md) and the
[KairoRenderer frame walkthrough](https://github.com/swayam8624/KairoRenderer/blob/docs/teaching-walkthrough-20261121/docs/FRAME_LIFECYCLE_WALKTHROUGH.md).

The main distinction is that an *identity*, a *file path*, an *imported CPU
artifact* and a *GPU resource handle* are four different things.

## 1. Stable identity versus a source location

[`AssetID.cppm`](../AssetID.cppm) defines a persistent UUID-shaped identity.
[`AssetMetadata.cppm`](../AssetMetadata.cppm) and
[`AssetTypes.cppm`](../AssetTypes.cppm) define type, origin, revision, path,
importer and dependency records.

An authored scene should hold `AssetID`, not a fragile absolute path.
Moving a registered asset changes its normalized portable path/revision but
preserves the ID. IDs also prevent two same-named files in different folders
from being confused. Typed handles require the resolved metadata type to
match the caller's expectation.

These invariants matter because Editor, EngineCore and Renderer may each be
running or writing different state when an asset is moved.

## 2. Registry ownership and transactional mutation

[`AssetRegistry.cppm`](../AssetRegistry.cppm) owns the authoritative maps
from persistent ID and case-folded path key to metadata. Reads use a shared
lock and return **copies**; callers must not hold references into the map
after the lock is released. Mutations take an exclusive lock.

`Create` generates a checked ID, `Move` validates the new path before
changing its index and incrementing the revision, `SetDependencies` checks
typed targets and rejects cycles, and `Remove` refuses to delete an asset
that still has dependents.

`ReplaceAll` is especially important: it builds and fully validates
candidate ID/path maps *before* swapping them into the registry under a
single lock. This protects readers from a half-loaded manifest. It is not
the same as a filesystem transaction covering every external asset file.

**Exercise:** Register mesh, texture and material records, make a material
depend on the texture, then attempt to remove that texture. Confirm the
registry rejects deletion until its dependent reference is changed.

## 3. Deterministic manifest persistence

[`AssetManifest.cppm`](../AssetManifest.cppm) serializes a detached
registry snapshot, orders assets by ID and sorts dependency records. It
quotes paths/importer identifiers so source control diffs are predictable.
The parser enforces syntax, size limits and line/column error positions.

`LoadAssetManifest` reads a bounded file, parses records, and calls
`ReplaceAll`; a failure should preserve the prior live graph.
`SaveAssetManifest` writes a sibling temporary file, flushes it, then
calls [`AtomicFile.cppm`](../AtomicFile.cppm) to replace the destination.
The file replacement is intended to prevent a partially written manifest
from appearing to readers.

The manifest stores *references and metadata*, not decoded mesh buffers
or GPU objects.

**Exercise:** Round-trip a registry through manifest serialization. Shuffle
input records and verify deterministic output; inject an unknown dependency
or malformed quoted field and verify the original live registry is preserved.

## 4. How source changes become reproducible import decisions

[`AssetFingerprint.cppm`](../AssetFingerprint.cppm) supplies SHA-256
content fingerprints; [`ImportDatabase.cppm`](../ImportDatabase.cppm)
tracks source fingerprint plus importer identity/version/settings.
A source file whose timestamp changes without byte-content change should
not necessarily require recompilation; conversely, changing importer
settings must produce a new derived recipe even if the source bytes match.

[`DerivedDataCache.cppm`](../DerivedDataCache.cppm) hashes the canonical
recipe consisting of source digest **and byte count**, expected output type,
importer identifier/version and settings into a `DerivedDataKey`.
`Contains` and `Load` deal in disk bytes; cache misses are expected.
`Store` rejects different bytes for an already-published key, uses an
exclusively claimed staging directory and creates a hard link to publish
the immutable target without clobbering a concurrent writer's entry.

This is a *content-keyed storage* system, not an end-to-end importer
scheduler or a GPU residency cache. It does not independently prove
the semantic correctness of a binary payload.

**Exercise:** Keep source data fixed while changing a single importer
setting or version. Verify keys differ. Then store a key twice with the
same bytes and with altered bytes, observing immutable-cache behavior.

## 5. Concrete importers and dependency safety

[`Importer.cppm`](../Importer.cppm) specifies the common importer API.
[`ImporterRegistry.cppm`](../ImporterRegistry.cppm) registers immutable
implementations by identifier **and version**. Coexisting versions preserve
the ability to read a project whose artifact recipe was created by an
older importer. The registry is not a dynamic-library loader.

Concrete paths include [`OBJImporter.cppm`](../OBJImporter.cppm),
[`TextureImporter.cppm`](../TextureImporter.cppm),
[`WavImporter.cppm`](../WavImporter.cppm),
[`GltfImporter.cppm`](../GltfImporter.cppm) and
[`GLBImporter.cppm`](../GLBImporter.cppm).

The glTF importer uses the external `cgltf` parser. A local `DataOwner`
RAII type releases C parser allocations on error. External file URIs,
accessors, buffer ranges, node transforms, primitive materials and declared
paths must be validated before conversion to the portable artifact.
Do not assume a path inside a glTF JSON document is safe merely because
the containing scene file is inside the project.

The current glTF scene importer retains versioned implementations:
select by recorded identity rather than automatically substituting a new
version that may change imported output.

## 6. Portable artifacts versus native renderer resources

[`MeshArtifact.cppm`](../MeshArtifact.cppm),
[`MaterialArtifact.cppm`](../MaterialArtifact.cppm),
[`TextureArtifact.cppm`](../TextureArtifact.cppm),
[`AudioArtifact.cppm`](../AudioArtifact.cppm) and
[`GltfSceneArtifact.cppm`](../GltfSceneArtifact.cppm) define portable
structured content. Binary format readers/writers and
[`DerivedArtifact.cppm`](../DerivedArtifact.cppm) establish the file payload
boundary, separate from external source formats.

The renderer's
[`AssetAdapters.cppm`](https://github.com/swayam8624/KairoRenderer/blob/docs/teaching-walkthrough-20261121/AssetAdapters.cppm)
converts authored materials/scenes to GPU-neutral renderer descriptions.
Only the selected Vulkan/Metal/OpenGL/D3D12 backend actually creates
device-resident buffers/textures; KairoAssets never takes that ownership.

**Exercise:** Follow one authored glTF texture reference through the
registry, artifact, `MaterialTextureResolver`, renderer handle and
backend destruction path. Explicitly record where the data changes from
path to stable ID to bytes to GPU handle.

## 7. Publish and live authoring boundaries

[`PipelinePublish.cppm`](../PipelinePublish.cppm) validates external DCC
bundle descriptors, source paths, file roles and fingerprints before
registering the Blender/glTF publish in the asset registry. A fingerprint
is an integrity check, not authorization to redistribute a third-party
model or photograph.

Editable mesh and authoring APIs live in
[`EditableMesh.cppm`](../EditableMesh.cppm),
[`EditableMeshDocument.cppm`](../EditableMeshDocument.cppm),
[`ModelingOperations.cppm`](../ModelingOperations.cppm),
[`AdvancedModelingOperations.cppm`](../AdvancedModelingOperations.cppm),
[`UVAuthoring.cppm`](../UVAuthoring.cppm),
[`MaterialAuthoring.cppm`](../MaterialAuthoring.cppm),
[`Sculpting.cppm`](../Sculpting.cppm) and
[`SculptProduction.cppm`](../SculptProduction.cppm).
Those modules concern editability of authored content; generated
runtime artifacts are a separate publication step.

[`SourceWatcher.cppm`](../SourceWatcher.cppm) is an explicitly requested
observation service; importing Kairo.Assets does not start watching the
filesystem in the background.

## 8. Learning tests and publishing requirements

Read [`tests/AssetTests.cpp`](../tests/AssetTests.cpp),
[`tests/GLBImporterTests.cpp`](../tests/GLBImporterTests.cpp),
[`tests/GltfSceneArtifactTests.cpp`](../tests/GltfSceneArtifactTests.cpp),
[`tests/TextureArtifactTests.cpp`](../tests/TextureArtifactTests.cpp),
[`tests/PipelinePublishTests.cpp`](../tests/PipelinePublishTests.cpp) and
[`tests/AssetStressTests.cpp`](../tests/AssetStressTests.cpp).
Before claiming complete project integrity, validate malformed source
paths, giant inputs, dependency cycles, concurrent registration, cache
publication races and binary format corruption.

The repository's code license can permit studying/modifying its original
code, but third-party `cgltf` code, imported assets, fonts, textures,
model files and datasets retain their own notices. Maintain an explicit
provenance and attribution record for redistributable bundles.

**Evidence standard:** identify whether a behavior is source-present,
unit-tested, host-verified or stress-tested. This guide was written by
source inspection, not by running a new build or importer benchmark.
