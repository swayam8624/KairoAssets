// TEACHING ENTRY POINT: Public C++23 umbrella for persistent asset identity,
// source metadata, import pipelines, derived artifacts and editable authoring.
//
// Source-to-render flow:
//   AssetID/Registry/Manifest: stable identity + dependency/path invariants
//   Fingerprint/ImportDatabase: source-content revision/provenance detection
//   ImporterRegistry + concrete importers: validated source decoding
//   DerivedDataCache/DerivedArtifact: immutable, content-keyed CPU artifacts
//   Mesh/Material/Texture/GltfScene artifacts: portable runtime payloads
//   Renderer::AssetAdapters: convert *those* payloads into GPU handles
//
// Exporting a module does not load content, create a Vulkan resource, or
// start file observation. SourceWatcher is an optional explicitly instantiated
// service. Source asset references remain stable across path changes; renderer
// handles are transient and owned by the graphics backend.
//
// Preserve third-party attribution for imported media and external libraries:
// an MIT code license does not grant rights to all files a user imports.
// See docs/SOURCE_READING_ORDER.md and tests/AssetTests.cpp.
export module Kairo.Assets;

export import Kairo.Assets.Types;
export import Kairo.Assets.AssetID;
export import Kairo.Assets.AtomicFile;
export import Kairo.Assets.Fingerprint;
export import Kairo.Assets.ImportDatabase;
export import Kairo.Assets.DerivedDataCache;
export import Kairo.Assets.BinaryFormat;
export import Kairo.Assets.DerivedArtifact;
export import Kairo.Assets.MeshArtifact;
export import Kairo.Assets.EditableMesh;
export import Kairo.Assets.ModelingOperations;
export import Kairo.Assets.AdvancedModelingOperations;
export import Kairo.Assets.UVAuthoring;
export import Kairo.Assets.MaterialAuthoring;
export import Kairo.Assets.EditableMeshDocument;
export import Kairo.Assets.Sculpting;
export import Kairo.Assets.SculptProduction;
export import Kairo.Assets.TextureArtifact;
export import Kairo.Assets.AudioArtifact;
export import Kairo.Assets.MaterialArtifact;
export import Kairo.Assets.GltfSceneArtifact;
export import Kairo.Assets.SourceWatcher;
export import Kairo.Assets.Importer;
export import Kairo.Assets.ImporterRegistry;
export import Kairo.Assets.OBJImporter;
export import Kairo.Assets.TextureImporter;
export import Kairo.Assets.WavImporter;
export import Kairo.Assets.GltfImporter;
export import Kairo.Assets.GLBImporter;
export import Kairo.Assets.BuiltinImporters;
export import Kairo.Assets.PipelinePublish;
export import Kairo.Assets.Metadata;
export import Kairo.Assets.Registry;
export import Kairo.Assets.Manifest;
