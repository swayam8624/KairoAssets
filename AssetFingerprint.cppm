module;

#include <algorithm>
#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <limits>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>

export module Kairo.Assets.Fingerprint;

export namespace kairo::assets
{
    /// Stable content identity for an asset source or derived artifact. The
    /// digest is SHA-256 over exactly ByteCount bytes, encoded in lowercase
    /// hexadecimal for manifests. It is intentionally independent of source
    /// filename, timestamp, platform, and filesystem metadata.
    struct AssetFingerprint final
    {
        std::array<std::byte, 32u> Digest{};
        std::uint64_t ByteCount = 0u;

        [[nodiscard]] std::string ToHex() const;
        [[nodiscard]] static AssetFingerprint Parse(std::string_view hex, std::uint64_t byteCount);

        [[nodiscard]] friend constexpr bool operator==(const AssetFingerprint& left,
            const AssetFingerprint& right) noexcept
        {
            if (left.ByteCount != right.ByteCount) return false;
            for (std::size_t index = 0u; index < left.Digest.size(); ++index)
                if (left.Digest[index] != right.Digest[index]) return false;
            return true;
        }
    };

    /// Input: arbitrary byte content. Output: its stable SHA-256 fingerprint.
    /// Task: provide source invalidation that is correct after file restores,
    /// clock changes, VCS checkouts, or cross-machine copies. The function is
    /// allocation-free after the caller supplies the input span.
    [[nodiscard]] AssetFingerprint FingerprintBytes(std::span<const std::byte> bytes);

    /// Input: a regular readable source file. Output: SHA-256 fingerprint
    /// computed in bounded chunks. Task: avoid loading multi-gigabyte assets
    /// just to decide whether reimport is required. Symlinks are resolved by
    /// the host filesystem; non-regular/missing files fail explicitly.
    [[nodiscard]] AssetFingerprint FingerprintFile(const std::filesystem::path& path);
}

namespace kairo::assets::fingerprint_detail
{
    [[nodiscard]] constexpr std::uint32_t RotateRight(std::uint32_t value, unsigned shift) noexcept
    {
        return std::rotr(value, static_cast<int>(shift));
    }

    class Sha256 final
    {
    public:
        void Update(std::span<const std::byte> bytes)
        {
            if (bytes.size() > std::numeric_limits<std::uint64_t>::max() - m_ByteCount)
                throw std::length_error("Asset source exceeds the SHA-256 byte-count limit.");
            m_ByteCount += static_cast<std::uint64_t>(bytes.size());
            std::size_t cursor = 0u;
            if (m_BufferSize != 0u)
            {
                const std::size_t copied = std::min(bytes.size(), m_Buffer.size() - m_BufferSize);
                std::copy_n(bytes.data(), copied, m_Buffer.data() + m_BufferSize);
                m_BufferSize += copied;
                cursor += copied;
                if (m_BufferSize == m_Buffer.size())
                {
                    Transform(m_Buffer);
                    m_BufferSize = 0u;
                }
            }
            while (cursor + m_Buffer.size() <= bytes.size())
            {
                Transform(bytes.subspan(cursor, m_Buffer.size()));
                cursor += m_Buffer.size();
            }
            if (cursor != bytes.size())
            {
                m_BufferSize = bytes.size() - cursor;
                std::copy_n(bytes.data() + cursor, m_BufferSize, m_Buffer.data());
            }
        }

        [[nodiscard]] AssetFingerprint Finalize() const
        {
            Sha256 finalized = *this;
            const std::uint64_t bitCount = finalized.m_ByteCount * 8u;
            finalized.m_Buffer[finalized.m_BufferSize++] = std::byte{ 0x80u };
            if (finalized.m_BufferSize > 56u)
            {
                std::fill(finalized.m_Buffer.begin() + static_cast<std::ptrdiff_t>(finalized.m_BufferSize),
                    finalized.m_Buffer.end(), std::byte{ 0u });
                finalized.Transform(finalized.m_Buffer);
                finalized.m_BufferSize = 0u;
            }
            std::fill(finalized.m_Buffer.begin() + static_cast<std::ptrdiff_t>(finalized.m_BufferSize),
                finalized.m_Buffer.begin() + 56, std::byte{ 0u });
            for (unsigned index = 0u; index < 8u; ++index)
                finalized.m_Buffer[56u + index] = std::byte{ static_cast<unsigned char>(bitCount >> ((7u - index) * 8u)) };
            finalized.Transform(finalized.m_Buffer);

            AssetFingerprint result;
            result.ByteCount = finalized.m_ByteCount;
            for (unsigned word = 0u; word < finalized.m_State.size(); ++word)
                for (unsigned byte = 0u; byte < 4u; ++byte)
                    result.Digest[word * 4u + byte] = std::byte{ static_cast<unsigned char>(
                        finalized.m_State[word] >> ((3u - byte) * 8u)) };
            return result;
        }

    private:
        std::array<std::uint32_t, 8u> m_State{
            0x6a09e667u, 0xbb67ae85u, 0x3c6ef372u, 0xa54ff53au,
            0x510e527fu, 0x9b05688cu, 0x1f83d9abu, 0x5be0cd19u };
        std::array<std::byte, 64u> m_Buffer{};
        std::size_t m_BufferSize = 0u;
        std::uint64_t m_ByteCount = 0u;

        /// Input: exactly one SHA-256 message block. Output: updates the
        /// chaining state in place. Task: keep the compression primitive
        /// independent from whether its caller owns a fixed buffer or views a
        /// larger streamed input range.
        void Transform(std::span<const std::byte> block)
        {
            if (block.size() != m_Buffer.size())
                throw std::logic_error("SHA-256 transform requires exactly one 64-byte block.");
            static constexpr std::array<std::uint32_t, 64u> constants{
                0x428a2f98u,0x71374491u,0xb5c0fbcfu,0xe9b5dba5u,0x3956c25bu,0x59f111f1u,0x923f82a4u,0xab1c5ed5u,
                0xd807aa98u,0x12835b01u,0x243185beu,0x550c7dc3u,0x72be5d74u,0x80deb1feu,0x9bdc06a7u,0xc19bf174u,
                0xe49b69c1u,0xefbe4786u,0x0fc19dc6u,0x240ca1ccu,0x2de92c6fu,0x4a7484aau,0x5cb0a9dcu,0x76f988dau,
                0x983e5152u,0xa831c66du,0xb00327c8u,0xbf597fc7u,0xc6e00bf3u,0xd5a79147u,0x06ca6351u,0x14292967u,
                0x27b70a85u,0x2e1b2138u,0x4d2c6dfcu,0x53380d13u,0x650a7354u,0x766a0abbu,0x81c2c92eu,0x92722c85u,
                0xa2bfe8a1u,0xa81a664bu,0xc24b8b70u,0xc76c51a3u,0xd192e819u,0xd6990624u,0xf40e3585u,0x106aa070u,
                0x19a4c116u,0x1e376c08u,0x2748774cu,0x34b0bcb5u,0x391c0cb3u,0x4ed8aa4au,0x5b9cca4fu,0x682e6ff3u,
                0x748f82eeu,0x78a5636fu,0x84c87814u,0x8cc70208u,0x90befffau,0xa4506cebu,0xbef9a3f7u,0xc67178f2u };
            std::array<std::uint32_t, 64u> words{};
            for (unsigned index = 0u; index < 16u; ++index)
            {
                const unsigned offset = index * 4u;
                words[index] = (static_cast<std::uint32_t>(std::to_integer<unsigned char>(block[offset])) << 24u) |
                    (static_cast<std::uint32_t>(std::to_integer<unsigned char>(block[offset + 1u])) << 16u) |
                    (static_cast<std::uint32_t>(std::to_integer<unsigned char>(block[offset + 2u])) << 8u) |
                    static_cast<std::uint32_t>(std::to_integer<unsigned char>(block[offset + 3u]));
            }
            for (unsigned index = 16u; index < words.size(); ++index)
            {
                const std::uint32_t sigma0 = RotateRight(words[index - 15u], 7u) ^ RotateRight(words[index - 15u], 18u) ^ (words[index - 15u] >> 3u);
                const std::uint32_t sigma1 = RotateRight(words[index - 2u], 17u) ^ RotateRight(words[index - 2u], 19u) ^ (words[index - 2u] >> 10u);
                words[index] = words[index - 16u] + sigma0 + words[index - 7u] + sigma1;
            }
            std::uint32_t a = m_State[0u]; std::uint32_t b = m_State[1u];
            std::uint32_t c = m_State[2u]; std::uint32_t d = m_State[3u];
            std::uint32_t e = m_State[4u]; std::uint32_t f = m_State[5u];
            std::uint32_t g = m_State[6u]; std::uint32_t h = m_State[7u];
            for (unsigned index = 0u; index < words.size(); ++index)
            {
                const std::uint32_t sum1 = RotateRight(e, 6u) ^ RotateRight(e, 11u) ^ RotateRight(e, 25u);
                const std::uint32_t choice = (e & f) ^ (~e & g);
                const std::uint32_t temporary1 = h + sum1 + choice + constants[index] + words[index];
                const std::uint32_t sum0 = RotateRight(a, 2u) ^ RotateRight(a, 13u) ^ RotateRight(a, 22u);
                const std::uint32_t majority = (a & b) ^ (a & c) ^ (b & c);
                const std::uint32_t temporary2 = sum0 + majority;
                h = g; g = f; f = e; e = d + temporary1; d = c; c = b; b = a; a = temporary1 + temporary2;
            }
            m_State[0u] += a; m_State[1u] += b; m_State[2u] += c; m_State[3u] += d;
            m_State[4u] += e; m_State[5u] += f; m_State[6u] += g; m_State[7u] += h;
        }
    };

    [[nodiscard]] inline unsigned HexNibble(char character)
    {
        if (character >= '0' && character <= '9') return static_cast<unsigned>(character - '0');
        if (character >= 'a' && character <= 'f') return static_cast<unsigned>(character - 'a' + 10);
        if (character >= 'A' && character <= 'F') return static_cast<unsigned>(character - 'A' + 10);
        throw std::invalid_argument("Asset fingerprint must contain hexadecimal characters only.");
    }
}

namespace kairo::assets
{
    std::string AssetFingerprint::ToHex() const
    {
        static constexpr char digits[] = "0123456789abcdef";
        std::string result;
        result.resize(Digest.size() * 2u);
        for (std::size_t index = 0u; index < Digest.size(); ++index)
        {
            const unsigned value = std::to_integer<unsigned char>(Digest[index]);
            result[index * 2u] = digits[value >> 4u];
            result[index * 2u + 1u] = digits[value & 0x0fu];
        }
        return result;
    }

    AssetFingerprint AssetFingerprint::Parse(std::string_view hex, std::uint64_t byteCount)
    {
        if (hex.size() != 64u) throw std::invalid_argument("Asset fingerprint must contain exactly 64 hexadecimal characters.");
        AssetFingerprint result;
        result.ByteCount = byteCount;
        for (std::size_t index = 0u; index < result.Digest.size(); ++index)
            result.Digest[index] = std::byte{ static_cast<unsigned char>((fingerprint_detail::HexNibble(hex[index * 2u]) << 4u) |
                fingerprint_detail::HexNibble(hex[index * 2u + 1u])) };
        return result;
    }

    AssetFingerprint FingerprintBytes(std::span<const std::byte> bytes)
    {
        fingerprint_detail::Sha256 hash;
        hash.Update(bytes);
        return hash.Finalize();
    }

    AssetFingerprint FingerprintFile(const std::filesystem::path& path)
    {
        std::error_code error;
        if (!std::filesystem::is_regular_file(path, error) || error)
            throw std::invalid_argument("Asset fingerprint source must be a regular readable file: " + path.generic_string());
        std::ifstream input(path, std::ios::binary);
        if (!input) throw std::runtime_error("Unable to open asset fingerprint source: " + path.generic_string());
        fingerprint_detail::Sha256 hash;
        std::array<std::byte, 128u * 1024u> buffer{};
        while (input)
        {
            input.read(reinterpret_cast<char*>(buffer.data()), static_cast<std::streamsize>(buffer.size()));
            const std::streamsize count = input.gcount();
            if (count > 0) hash.Update(std::span<const std::byte>(buffer.data(), static_cast<std::size_t>(count)));
        }
        if (!input.eof()) throw std::runtime_error("Unable to read asset fingerprint source: " + path.generic_string());
        return hash.Finalize();
    }
}
