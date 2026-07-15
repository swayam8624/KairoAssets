module;

#include <array>
#include <compare>
#include <cstddef>
#include <cstdint>
#include <random>
#include <stdexcept>
#include <string>
#include <string_view>

export module Kairo.Assets.AssetID;

export namespace kairo::assets
{
    /// Persistent 128-bit asset identity encoded as a canonical UUID string.
    ///
    /// Input: Parse() accepts exactly `xxxxxxxx-xxxx-xxxx-xxxx-xxxxxxxxxxxx`
    /// with hexadecimal digits in either case.
    /// Output: ToString() always emits lowercase canonical text.
    /// Task: preserve references across path moves and renames. All-zero bytes
    /// are reserved as the invalid identity and are rejected by registries.
    class AssetID final
    {
    public:
        using Storage = std::array<std::uint8_t, 16>;

        constexpr AssetID() noexcept = default;
        explicit constexpr AssetID(Storage bytes) noexcept : m_Bytes(bytes) {}

        [[nodiscard]] constexpr bool IsValid() const noexcept
        {
            for (const std::uint8_t byte : m_Bytes)
                if (byte != 0u) return true;
            return false;
        }

        [[nodiscard]] constexpr const Storage& Bytes() const noexcept { return m_Bytes; }

        [[nodiscard]] std::string ToString() const
        {
            constexpr char digits[] = "0123456789abcdef";
            std::string result(36u, '-');
            std::size_t source = 0u;
            for (std::size_t destination = 0u; destination < result.size(); ++destination)
            {
                if (destination == 8u || destination == 13u || destination == 18u || destination == 23u) continue;
                const std::uint8_t byte = m_Bytes[source / 2u];
                result[destination] = digits[(source % 2u == 0u) ? (byte >> 4u) : (byte & 0x0fu)];
                ++source;
            }
            return result;
        }

        [[nodiscard]] static AssetID Parse(std::string_view text)
        {
            if (text.size() != 36u || text[8] != '-' || text[13] != '-' || text[18] != '-' || text[23] != '-')
                throw std::invalid_argument("AssetID must use canonical UUID formatting.");

            Storage bytes{};
            std::size_t nibble = 0u;
            for (std::size_t index = 0u; index < text.size(); ++index)
            {
                if (index == 8u || index == 13u || index == 18u || index == 23u) continue;
                const std::uint8_t value = HexValue(text[index]);
                if ((nibble % 2u) == 0u) bytes[nibble / 2u] = static_cast<std::uint8_t>(value << 4u);
                else bytes[nibble / 2u] = static_cast<std::uint8_t>(bytes[nibble / 2u] | value);
                ++nibble;
            }
            return AssetID{ bytes };
        }

        friend constexpr bool operator==(const AssetID& lhs, const AssetID& rhs) noexcept
        {
            return lhs.m_Bytes == rhs.m_Bytes;
        }

        /// Provides a lexicographic, byte-stable order for ordered registries
        /// and deterministic manifest output. The comparison is deliberately
        /// explicit instead of defaulted: some C++ module implementations do
        /// not expose std::array's synthesized three-way comparison reliably
        /// across an imported standard-library boundary.
        friend constexpr std::strong_ordering operator<=>(const AssetID& lhs,
            const AssetID& rhs) noexcept
        {
            for (std::size_t index = 0u; index < lhs.m_Bytes.size(); ++index)
            {
                if (lhs.m_Bytes[index] < rhs.m_Bytes[index]) return std::strong_ordering::less;
                if (lhs.m_Bytes[index] > rhs.m_Bytes[index]) return std::strong_ordering::greater;
            }
            return std::strong_ordering::equal;
        }

    private:
        Storage m_Bytes{};

        [[nodiscard]] static constexpr std::uint8_t HexValue(char character)
        {
            if (character >= '0' && character <= '9') return static_cast<std::uint8_t>(character - '0');
            if (character >= 'a' && character <= 'f') return static_cast<std::uint8_t>(character - 'a' + 10);
            if (character >= 'A' && character <= 'F') return static_cast<std::uint8_t>(character - 'A' + 10);
            throw std::invalid_argument("AssetID contains a non-hexadecimal character.");
        }
    };

    /// Output: random RFC 4122 version-4/variant-1 identity.
    /// Task: mint project asset identities. Collision handling still belongs
    /// to AssetRegistry, which retries rather than assuming randomness is proof.
    [[nodiscard]] inline AssetID GenerateAssetID()
    {
        thread_local std::mt19937_64 generator = []
        {
            std::random_device source;
            std::seed_seq seed{
                source(), source(), source(), source(), source(), source(), source(), source()
            };
            return std::mt19937_64(seed);
        }();

        AssetID::Storage bytes{};
        for (std::size_t offset = 0u; offset < bytes.size(); offset += sizeof(std::uint64_t))
        {
            const std::uint64_t value = generator();
            for (std::size_t index = 0u; index < sizeof(value); ++index)
                bytes[offset + index] = static_cast<std::uint8_t>(value >> (index * 8u));
        }
        bytes[6] = static_cast<std::uint8_t>((bytes[6] & 0x0fu) | 0x40u);
        bytes[8] = static_cast<std::uint8_t>((bytes[8] & 0x3fu) | 0x80u);
        return AssetID{ bytes };
    }

    /// Hash object supplied explicitly to unordered containers. Avoiding a
    /// `std::hash` specialization keeps the module's public API self-contained.
    struct AssetIDHash final
    {
        [[nodiscard]] std::size_t operator()(const AssetID& id) const noexcept
        {
            std::size_t hash = static_cast<std::size_t>(1469598103934665603ull);
            for (const std::uint8_t byte : id.Bytes())
            {
                hash ^= static_cast<std::size_t>(byte);
                hash *= static_cast<std::size_t>(1099511628211ull);
            }
            return hash;
        }
    };
}
