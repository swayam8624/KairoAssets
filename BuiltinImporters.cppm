module;

#include <memory>

export module Kairo.Assets.BuiltinImporters;

import Kairo.Assets.GltfImporter;
import Kairo.Assets.Importer;
import Kairo.Assets.ImporterRegistry;
import Kairo.Assets.OBJImporter;
import Kairo.Assets.TextureImporter;

export namespace kairo::assets
{
    inline void RegisterBuiltinImporters(ImporterRegistry& registry)
    {
        registry.Register(std::make_shared<PassthroughImporter>());
        registry.Register(std::make_shared<OBJMeshImporter>());
        registry.Register(std::make_shared<StbTextureImporter>());
        registry.Register(std::make_shared<GltfSceneImporter>());
    }
}
