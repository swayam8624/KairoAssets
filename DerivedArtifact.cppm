module;

#include <cstddef>
#include <cstdint>
#include <span>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

export module Kairo.Assets.DerivedArtifact;

import Kairo.Assets.Types;

export namespace kairo::assets
{
    /// Versioned, backend-neutral importer output. Payload is intentionally
    /// opaque to KairoAssets: runtime loaders own decoding while this layer
    /// guarantees the artifact's declared asset category and format revision.
    struct DerivedArtifact final
    {
        AssetType Type = AssetType::Other;
        std::uint32_t FormatVersion = 1u;
        std::string Format;
        std::vector<std::byte> Payload;
    };

    inline void ValidateDerivedArtifact(const DerivedArtifact& artifact)
    {
        if (artifact.FormatVersion == 0u) throw std::invalid_argument("Derived artifact format version must be positive.");
        if (artifact.Format.empty() || artifact.Format.size() > 128u)
            throw std::invalid_argument("Derived artifact format must contain 1 to 128 characters.");
        if (artifact.Payload.size() > 1024u * 1024u * 1024u)
            throw std::length_error("Derived artifact payload exceeds the 1 GiB safety limit.");
    }
}
