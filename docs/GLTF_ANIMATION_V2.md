# glTF Scene Artifact V2

`kairo.gltf-scene.v2` extends the existing hierarchy-preserving scene artifact with portable skeletal skinning and transform animation data. KairoAssets remains backend-neutral: it stores source semantics and validation rules but does not evaluate clips, build runtime poses, allocate GPU palettes, or depend on KairoEngineCore/KairoRenderer.

## Compatibility

The payload family keeps the existing `KGLTF001` magic and uses an explicit payload version. `ParseGltfSceneArtifactData()` accepts both payload v1 and v2. A legacy v1 scene produces the same static materials, primitives, nodes, and roots as before, with empty `Skinning`, `Skins`, and `Animations` fields and `GltfMissingIndex` node skin bindings.

The derived artifact envelope is stricter: `kairo.gltf-scene.v1` / format version 1 must wrap a payload-v1 body, while `kairo.gltf-scene.v2` / format version 2 must wrap payload v2. This prevents cache or provenance metadata from claiming one schema while carrying another.

The built-in importer registry preserves both `kairo.gltf.scene@1` and `kairo.gltf.scene@2`. Version 1 remains available for old import records and publishes the exact static v1 binary schema. It rejects skinning or animation instead of silently dropping v2-only semantics. Version 2 is the current importer and publishes the extended artifact. Keeping both identities means old provenance/cache keys remain reproducible after the v2 upgrade.

## Skinning

Each skinned primitive stores exactly four joint indices and four weights per vertex in `GltfVertexSkinData`. `GltfSceneImporter` reads glTF `JOINTS_0` and `WEIGHTS_0`, accepts unsigned-byte/unsigned-short joints and FLOAT or normalized unsigned weights, and canonicalizes the four weights to sum to one.

A `GltfSkinData` record stores the ordered joint-node palette, optional skeleton root, and one column-major inverse-bind matrix per joint. If glTF omits `inverseBindMatrices`, the importer materializes identity matrices so the artifact has one deterministic representation.

`GltfNodeData::SkinIndex` binds a mesh-bearing node to a skin. Artifact validation then checks non-zero influences against that skin's palette, rather than treating the raw `JOINTS_0` values as global node indices.

Scene artifact v2 intentionally supports one four-influence set. `JOINTS_1` / `WEIGHTS_1` and morph targets fail explicitly instead of being discarded.

## Animation

For nodes authored with glTF TRS properties, v2 also preserves `RestTranslation`, `RestRotation`, and `RestScale` plus a `HasRestTRS` marker. Animated targets are required to have this rest pose. This is deliberate: a clip often animates only one of translation/rotation/scale, and reconstructing the untouched properties later from the composed matrix is lossy or ambiguous under negative scale. Matrix-authored static nodes keep `HasRestTRS == false` and canonical unused rest fields.

`GltfAnimationClipData` contains node-targeted channels. V2 preserves three transform paths:

- translation — three-component values
- rotation — unit quaternion values
- scale — three-component values

Linear, Step, and CubicSpline interpolation are preserved. Cubic-spline keys retain the source in-tangent, value, and out-tangent. Quaternion key values are normalized during import; cubic quaternion tangents are preserved as derivatives and are not normalized.

Morph-weight animation is not represented by this schema and therefore fails explicitly at import.

## Validation

The artifact rejects malformed or ambiguous data before a runtime sees it, including:

- skin-influence counts that do not match primitive vertex counts
- negative, non-finite, or non-normalized weights
- invalid skin/joint/palette references
- duplicate joints and non-finite inverse-bind matrices
- skin bindings on nodes with no mesh primitive
- non-finite animation values or tangents
- invalid target nodes, paths, or interpolation modes
- animation targeting a node without preserved rest TRS
- empty channels and non-monotonic/negative key times
- duplicate channels targeting the same node/path within one clip
- non-unit rotation quaternion key values
- v1/v2 derived-envelope and payload-version mismatch

## Downstream boundary

This milestone only establishes portable source data. The next runtime milestones belong downstream:

1. KairoEngineCore: clip sampling, local TRS pose construction, looping/time policy, blending and cross-fades.
2. KairoRenderer: skin matrix palette generation/upload and skinned vertex execution.
3. KairoEditor: animation clip selection, playback/scrubbing, skeleton visualization, and pose inspection.

The EngineCore handoff is intentionally sufficient to sample a partial channel against a deterministic rest pose without matrix decomposition. Static matrix-authored nodes continue to use their preserved `LocalTransform`; only animated targets require source TRS.

That dependency direction keeps import workers, CI, command-line project tools, and headless asset processing usable without a graphics runtime.
