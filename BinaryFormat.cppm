module;

#include <bit>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <vector>

export module Kairo.Assets.BinaryFormat;

export namespace kairo::assets
{
    /// Deterministic little-endian writer for Kairo-owned artifact formats.
    /// It never serializes native structs, padding, enum ordinals, or host
    /// endianness. Callers remain responsible for format-specific size limits.
    class BinaryWriter final
    {
    public:
        explicit BinaryWriter(std::size_t reserveBytes = 0u)
        {
            m_Bytes.reserve(reserveBytes);
        }

        void WriteU8(std::uint8_t value) { m_Bytes.push_back(std::byte{ value }); }
        void WriteU32(std::uint32_t value) { WriteUnsigned(value); }
        void WriteU64(std::uint64_t value) { WriteUnsigned(value); }

        void WriteF32(float value)
        {
            static_assert(sizeof(float) == sizeof(std::uint32_t));
            static_assert(std::numeric_limits<float>::is_iec559);
            WriteU32(std::bit_cast<std::uint32_t>(value));
        }

        void WriteBytes(std::span<const std::byte> bytes)
        {
            m_Bytes.insert(m_Bytes.end(), bytes.begin(), bytes.end());
        }

        void WriteText(std::string_view text)
        {
            for (const char character : text)
                m_Bytes.push_back(std::byte{ static_cast<unsigned char>(character) });
        }

        [[nodiscard]] const std::vector<std::byte>& Bytes() const noexcept { return m_Bytes; }
        [[nodiscard]] std::vector<std::byte> TakeBytes() && noexcept { return std::move(m_Bytes); }

    private:
        std::vector<std::byte> m_Bytes;

        template<class Integer>
        void WriteUnsigned(Integer value)
        {
            static_assert(std::is_unsigned_v<Integer>);
            for (std::size_t index = 0u; index < sizeof(Integer); ++index)
                WriteU8(static_cast<std::uint8_t>(value >> (index * 8u)));
        }
    };

    /// Bounds-checked reader paired with BinaryWriter. Every failed read throws
    /// before advancing the cursor, and RequireEnd() rejects trailing bytes.
    class BinaryReader final
    {
    public:
        explicit BinaryReader(std::span<const std::byte> bytes) noexcept : m_Bytes(bytes) {}

        [[nodiscard]] std::uint8_t ReadU8()
        {
            RequireAvailable(1u);
            return std::to_integer<std::uint8_t>(m_Bytes[m_Cursor++]);
        }

        [[nodiscard]] std::uint32_t ReadU32() { return ReadUnsigned<std::uint32_t>(); }
        [[nodiscard]] std::uint64_t ReadU64() { return ReadUnsigned<std::uint64_t>(); }

        [[nodiscard]] float ReadF32()
        {
            static_assert(sizeof(float) == sizeof(std::uint32_t));
            static_assert(std::numeric_limits<float>::is_iec559);
            return std::bit_cast<float>(ReadU32());
        }

        [[nodiscard]] std::span<const std::byte> ReadBytes(std::size_t count)
        {
            RequireAvailable(count);
            const auto result = m_Bytes.subspan(m_Cursor, count);
            m_Cursor += count;
            return result;
        }

        [[nodiscard]] std::string ReadText(std::size_t count)
        {
            const auto bytes = ReadBytes(count);
            std::string result(count, '\0');
            for (std::size_t index = 0u; index < count; ++index)
                result[index] = static_cast<char>(std::to_integer<unsigned char>(bytes[index]));
            return result;
        }

        [[nodiscard]] std::size_t Remaining() const noexcept { return m_Bytes.size() - m_Cursor; }
        [[nodiscard]] bool AtEnd() const noexcept { return m_Cursor == m_Bytes.size(); }

        void RequireEnd() const
        {
            if (!AtEnd()) throw std::invalid_argument("Binary input contains trailing bytes.");
        }

    private:
        std::span<const std::byte> m_Bytes;
        std::size_t m_Cursor = 0u;

        void RequireAvailable(std::size_t count) const
        {
            if (count > Remaining()) throw std::invalid_argument("Binary input is truncated.");
        }

        template<class Integer>
        [[nodiscard]] Integer ReadUnsigned()
        {
            static_assert(std::is_unsigned_v<Integer>);
            RequireAvailable(sizeof(Integer));
            Integer result = 0u;
            for (std::size_t index = 0u; index < sizeof(Integer); ++index)
                result |= static_cast<Integer>(ReadU8()) << (index * 8u);
            return result;
        }
    };
}
