# KairoAssets

`KairoAssets` is Kairo's backend-neutral asset identity, metadata registry, and
project manifest layer. It solves the persistent-reference problem between
authoring tools and runtime systems without owning Vulkan objects, decoded mesh
bytes, audio devices, scene ECS storage, or platform file watchers.

```text
KairoAssets ----------------------> KairoEngineCore scene references
     |                                      |
     +--> importer/runtime cache later      +--> Renderer/Physics adapters
```

## Current Surface

- random RFC 4122 version-4 128-bit `AssetID` values
- canonical lowercase UUID parsing and formatting
- compile-time typed mesh, material, texture, and scene handles
- portable project-relative path normalization
- case-folded path uniqueness for cross-platform manifests
- validated source, generated, and builtin asset origins
- asset type, importer, revision, and dependency metadata
- first-class `document` assets for typed graph/text authoring files
- thread-safe identity and path lookup
- atomic create, move, dependency update, and protected removal operations
- dependency existence/type validation and cycle rejection
- strong-exception-guarantee bulk manifest replacement
- deterministic ID-sorted snapshots and manifests
- exact line/column manifest syntax errors
- bounded parser input, asset count, path, importer, and dependency limits
- same-directory temporary writes followed by atomic host replacement
- deterministic SHA-256 fingerprints for in-memory and streamed source bytes
- thread-safe source-import provenance records with current/changed/missing checks
- disk-backed content-addressed derived-data cache with byte-exact round trips

The current milestone includes identity, deterministic manifest persistence, and
source-provenance invalidation. Importer plugins, decoded-resource caching,
file observation, thumbnailing, and renderer upload are separate runtime
services built on this contract; they are not silently simulated here.

## Build

```bash
cd /Users/swayamsingal/Desktop/Programming/Kairo/KairoAssets
cmake -S . -B build -G Ninja \
  -DCMAKE_CXX_COMPILER=/opt/homebrew/opt/llvm/bin/clang++
cmake --build build
ctest --test-dir build --output-on-failure
```

Use the umbrella module:

```cpp
import Kairo.Assets;

using namespace kairo::assets;

AssetRegistry assets;
const AssetID cube = assets.Create({
    AssetType::Mesh,
    AssetOrigin::Builtin,
    "builtin/cube",
    "kairo.builtin",
    {}
});

const AssetMetadata metadata = assets.Resolve(MeshAssetHandle{ cube });
SaveAssetManifest("Project/Assets.kassets", assets);
```

## Identity And Paths

An `AssetID` identifies authored content independently from its path. Moving
`meshes/props/chair.obj` to `environment/furniture/chair.obj` changes metadata
and increments its revision but leaves every scene reference intact.

Paths are logical project paths, not process-working-directory paths. They must
be relative, cannot contain parent traversal, roots, drive prefixes, colons,
backslashes, or control bytes, and are indexed case-insensitively. This rejects
a project containing both `Textures/Brick.png` and `textures/brick.png` before
it becomes ambiguous on a case-insensitive filesystem.

Importer identifiers contain 1 to 128 ASCII alphanumeric, `.`, `_`, or `-`
characters. They identify the importer implementation and version policy, not a
shared-library filename.

## Dependency Rules

Dependencies carry both identity and expected type. Registry mutation rejects:

- missing targets
- type mismatches
- self references
- duplicate dependencies
- direct or transitive cycles
- removal of an asset that still has dependents

Callers must explicitly redirect or remove dependents before deleting a shared
texture, material, mesh, or scene. There is no implicit cascade deletion.

## Manifest Format

The deterministic text format is intended for source control:

```text
kairo-assets 1
asset 00000000-0000-4000-8000-000000000001 texture2d source 2 "textures/brick.png" "kairo.image"
end
asset 00000000-0000-4000-8000-000000000002 material source 4 "materials/brick.kmat" "kairo.material-v1"
dependency 00000000-0000-4000-8000-000000000001 texture2d
end
```

Blank lines and `#` comments are accepted. Quoted values support `\\`, `\"`,
`\n`, and `\t`. Records may appear in any order because loading validates the
complete dependency graph before replacing live registry state. A parse or
graph error leaves the existing registry unchanged.

## Threading Contract

Registry reads use shared locking and return metadata copies. Mutations use
exclusive locking and never expose references into internal storage. Generated
IDs are checked and inserted under the same lock, so even an improbable random
collision cannot race another creator.

## Import Provenance

`AssetFingerprint` is a SHA-256 digest plus the exact byte count. It uses
content identity rather than timestamps, so a restored checkout or copied file
is correctly recognized as unchanged. `FingerprintFile()` streams in fixed-size
chunks and never loads an entire large source file into memory.

`ImportDatabase` records the last successful source fingerprint, importer
identifier/version, and canonical importer settings for registered
`AssetOrigin::SourceFile` assets. Its `Evaluate(projectRoot, asset)` result is:

- `Current` when the source content equals the recorded fingerprint
- `Changed` when it exists but its content differs
- `Missing` when it no longer exists as a regular file

This database deliberately does not execute an importer, watch directories, or
manage a derived-data cache. Those follow-on services consume this provenance
contract rather than duplicating its source-change logic.

## Derived Data Cache

`DerivedDataCache` stores immutable importer artifacts under a SHA-256 key.
`MakeDerivedDataKey()` includes the source fingerprint, importer identifier,
importer version, expected asset type, and canonical settings, so changing any
input cannot reuse a stale or mistyped artifact. It provides content-addressed `Store`, `Load`, and `Contains`
operations only; eviction policy, background importing, and file watching are
deliberately separate orchestration concerns.

## Source Observation

`SourceWatcher` is a deterministic polling baseline over `ImportDatabase`.
After `Synchronize()`, `Poll()` emits only state transitions in asset-ID order:
source content changes, disappearance, and a subsequent restored/current state
after provenance is updated. It emits no startup noise and no repeated event
while a source remains in the same state. Platform-native watcher adapters may
wake polling early, but this contract remains portable for CI and headless
tools.

## Import Execution

`AssetImporter` is a pure source-to-artifact plugin contract. `ImportSourceAsset()`
owns the transaction: it validates the selected importer against registry
metadata, reads the project-relative source, fingerprints it, builds a derived
cache key, validates the plugin's declared type, serializes a portable artifact
envelope, publishes it, and only then records successful provenance. Cache hits
are parsed and type-checked too, so corrupt or stale cache data cannot silently
enter runtime loading. `PassthroughImporter` is a working typed importer for
opaque documents, scripts, and raw source artifacts. Mesh/material/texture
importers use this transaction rather than owning cache or reimport rules.

`ImporterRegistry` owns immutable importer plugins and resolves exact
identifier/version pairs. Multiple versions may coexist for reproducible old
projects, duplicate identities are rejected, and snapshots are deterministic
for diagnostics or editor presentation. It deliberately does not load dynamic
libraries; a future signed plugin loader will publish validated instances into
this same registry.

`DerivedArtifact` defines the versioned output envelope for those plugins:
declared asset type, stable format identifier, positive format version, and
opaque payload. `SerializeDerivedArtifact()` writes an endian-independent
`KAIRODD1` binary envelope using stable asset-type names, while
`ParseDerivedArtifact()` rejects unknown versions/types, truncation, oversized
fields, malformed format names, and trailing bytes. Runtime loaders own the
meaning of each validated payload format.

All Kairo-owned artifact payloads use the shared `BinaryWriter` and
`BinaryReader` contract. It writes explicit little-endian integers and IEEE-754
32-bit floats rather than native structs or enum ordinals; every read is bounds
checked and complete parsers call `RequireEnd()` to reject trailing data. This
keeps mesh, texture, material, and scene formats consistent and portable.

`MeshArtifactData` is the first concrete runtime-neutral payload: indexed
counter-clockwise triangles with object-local positions, optional normalized
vertex normals and UVs, and persisted bounds. Validation rejects non-finite
channels, invalid indices, non-unit normals, non-canonical absent channels,
degenerate triangles, count/size overflow, corrupted bounds, and trailing data.
Both KairoRenderer and KairoRayTracer can consume `kairo.mesh.v1` without
depending on each other's CPU or GPU types.

`OBJMeshImporter` is the first production source decoder. It handles 1-based
and relative indices, all standard face vertex forms, normalized source
normals, smoothing groups, area-weighted generated normals, homogeneous
positions, UVs, tuple deduplication, and ear-clipped planar concave polygons.
Diagnostics carry exact line/column locations. Material partitions, lines, and
points fail explicitly because mesh artifact v1 cannot preserve those semantics;
the importer never silently discards authored render data.

## Next Asset Milestones

```text
A1 stable identity + typed metadata + registry       complete
A2 deterministic validated manifest persistence      complete
A3 source fingerprints + validated import provenance     complete
A4 content-addressed derived-data cache                    complete
A5 portable source observation and reimport signals         complete
A6 executable typed importer transaction + raw importer     complete
A7 version-aware importer plugin registry                     complete
A8 portable mesh artifact + strict OBJ geometry importer       complete
A9 glTF/material/texture/scene decoder plugins                  next
A10 editor asset browser, thumbnails, drag/drop
```

Runtime loaders will depend on `KairoAssets`; this repository will not depend
on KairoRenderer or KairoEngineCore. That direction keeps headless builds,
import workers, tests, and command-line project tools usable without a GPU.
