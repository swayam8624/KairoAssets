# Phase 2 — Production Asset Pipeline

This phase establishes deterministic, cacheable source-to-artifact conversion for the content types required by a small 3D game.

## Implemented

- `TextureArtifact` stores validated RGBA8 or RGBA32F mip chains with explicit linear/sRGB metadata.
- Colour textures use linear-light mip filtering; normal-map mips are renormalized.
- `StbTextureImporter` decodes PNG, JPEG, TGA, BMP, PSD, GIF, PIC, PNM and HDR sources through a pinned stb revision.
- Texture settings are canonical and fingerprintable: colour space, normal-map classification, mip generation and maximum dimension.
- `MaterialArtifact` stores metallic-roughness PBR factors, alpha policy, double-sided state and typed texture asset references.
- `GltfSceneArtifact` stores hierarchy, local transforms, triangle primitives, tangent channels, PBR material metadata and source texture URIs.
- `GltfSceneImporter` accepts glTF 2.0 JSON and GLB, resolves embedded or sibling buffers through a pinned cgltf revision, rejects unsupported primitive modes and validates all generated geometry.
- `RegisterBuiltinImporters` publishes exact importer identities for reproducible project loading.

## Artifact contracts

| Source | Asset type | Importer | Artifact |
|---|---|---|---|
| OBJ | Mesh | `kairo.obj@1` | `kairo.mesh.v1` |
| PNG/JPEG/TGA/HDR and stb formats | Texture2D | `kairo.texture.stb@1` | `kairo.texture2d.v1` |
| glTF/GLB | Scene | `kairo.gltf.scene@1` | `kairo.gltf-scene.v1` |
| Opaque source | Declared type | `kairo.passthrough@1` | `kairo.raw.v1` |

The glTF scene artifact deliberately preserves texture URIs instead of silently creating registry assets. The editor import transaction owns that expansion so generated texture identities, dependencies and undo behaviour remain project-controlled.

## Acceptance gates

- Texture, material and glTF artifacts round-trip byte-for-byte through their typed parsers.
- A data-URI glTF scene imports with hierarchy, transforms, normals, UVs and indices intact.
- Malformed dimensions, non-finite values, invalid references and unsafe record counts fail before cache publication.
- Component tests run on Ubuntu Clang, macOS Clang and Windows MSVC.
