#include <catch2/catch_test_macros.hpp>

#include <cstddef>
#include <filesystem>
#include <string>
#include <vector>

import Kairo.Assets;

using namespace kairo::assets;

TEST_CASE("derived data cache preserves hundreds of immutable content-addressed entries",
    "[Assets][Scale][DDC]")
{
    const auto root = std::filesystem::temp_directory_path() /
        "kairo-assets-ddc-scale-stress";
    std::error_code cleanupError;
    std::filesystem::remove_all(root, cleanupError);
    DerivedDataCache cache(root);

    constexpr std::size_t entryCount = 256u;
    constexpr std::size_t entryBytes = 4096u;
    std::vector<DerivedDataKey> keys;
    keys.reserve(entryCount);

    for (std::size_t index = 0u; index < entryCount; ++index)
    {
        std::vector<std::byte> bytes(entryBytes);
        for (std::size_t byte = 0u; byte < bytes.size(); ++byte)
            bytes[byte] = static_cast<std::byte>((index * 31u + byte * 17u) & 0xffu);

        const AssetFingerprint source = FingerprintBytes(bytes);
        const DerivedDataKey key = MakeDerivedDataKey(
            source, AssetType::Mesh, "kairo.scale-stress", "1",
            "entry=" + std::to_string(index));
        cache.Store(key, bytes);
        CHECK(cache.Contains(key));
        CHECK(cache.Load(key) == bytes);
        keys.push_back(key);
    }

    REQUIRE(keys.size() == entryCount);
    for (const auto& key : keys) CHECK(cache.Contains(key));

    std::filesystem::remove_all(root, cleanupError);
}
