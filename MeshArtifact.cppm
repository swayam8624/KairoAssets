module;

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <span>
#include <stdexcept>
#include <utility>
#include <vector>

export module Kairo.Assets.MeshArtifact;

import Kairo.Assets.BinaryFormat;
import Kairo.Assets.DerivedArtifact;
import Kairo.Assets.Types;

export namespace kairo::assets
{
    /// Backend-neutral object-local vertex. Kairo uses right-handed coordinates
    /// and counter-clockwise front faces. Normals are unit length when present;
    /// UV origin semantics remain material/texture-loader policy.
    struct MeshArtifactVertex final
    {
        std::array<float, 3u> Position{};
        std::array<float, 3u> Normal{};
        std::array<float, 2u> TexCoord{};

        friend bool operator==(const MeshArtifactVertex&, const MeshArtifactVertex&) = default;
    };

    struct MeshArtifactBounds final
    {
        std::array<float, 3u> Minimum{};
        std::array<float, 3u> Maximum{};

        friend bool operator==(const MeshArtifactBounds&, const MeshArtifactBounds&) = default;
    };

    /// Indexed triangle-list payload shared by real-time and offline renderers.
    /// Missing channels are represented canonically by zero-filled vertex fields.
    struct MeshArtifactData final
    {
        std::vector<MeshArtifactVertex> Vertices;
        std::vector<std::uint32_t> Indices;
        bool HasNormals = false;
        bool HasTexCoords = false;

        friend bool operator==(const MeshArtifactData&, const MeshArtifactData&) = default;
    };

    namespace mesh_artifact_detail
    {
        constexpr std::array<std::byte, 8u> Magic{
            std::byte{ 'K' }, std::byte{ 'M' }, std::byte{ 'E' }, std::byte{ 'S' },
            std::byte{ 'H' }, std::byte{ '0' }, std::byte{ '0' }, std::byte{ '1' } };
        constexpr std::uint32_t PayloadVersion = 1u;
        constexpr std::uint32_t NormalsFlag = 1u << 0u;
        constexpr std::uint32_t TexCoordsFlag = 1u << 1u;
        constexpr std::uint32_t KnownFlags = NormalsFlag | TexCoordsFlag;
        constexpr std::size_t HeaderBytes = 8u + 4u * 4u + 6u * 4u;

        [[nodiscard]] inline bool IsFinite(const std::array<float, 3u>& value) noexcept
        {
            return std::isfinite(value[0]) && std::isfinite(value[1]) && std::isfinite(value[2]);
        }

        [[nodiscard]] inline bool IsFinite(const std::array<float, 2u>& value) noexcept
        {
            return std::isfinite(value[0]) && std::isfinite(value[1]);
        }

        [[nodiscard]] inline std::array<double, 3u> Subtract(
            const std::array<float, 3u>& left, const std::array<float, 3u>& right) noexcept
        {
            return { static_cast<double>(left[0]) - right[0], static_cast<double>(left[1]) - right[1],
                static_cast<double>(left[2]) - right[2] };
        }

        [[nodiscard]] inline double LengthSquared(const std::array<double, 3u>& value) noexcept
        {
            return value[0] * value[0] + value[1] * value[1] + value[2] * value[2];
        }

        [[nodiscard]] inline std::array<double, 3u> Cross(
            const std::array<double, 3u>& left, const std::array<double, 3u>& right) noexcept
        {
            return { left[1] * right[2] - left[2] * right[1],
                left[2] * right[0] - left[0] * right[2],
                left[0] * right[1] - left[1] * right[0] };
        }

        [[nodiscard]] inline std::size_t PayloadSize(
            std::size_t vertexCount, std::size_t indexCount, bool normals, bool texCoords)
        {
            const std::size_t vertexStride = 3u * sizeof(float) +
                (normals ? 3u * sizeof(float) : 0u) + (texCoords ? 2u * sizeof(float) : 0u);
            if (vertexCount > (MaximumDerivedArtifactPayloadBytes - HeaderBytes) / vertexStride)
                throw std::length_error("Mesh artifact vertex payload exceeds its safety limit.");
            const std::size_t afterVertices = HeaderBytes + vertexCount * vertexStride;
            if (indexCount > (MaximumDerivedArtifactPayloadBytes - afterVertices) / sizeof(std::uint32_t))
                throw std::length_error("Mesh artifact index payload exceeds its safety limit.");
            return afterVertices + indexCount * sizeof(std::uint32_t);
        }
    }

    [[nodiscard]] inline MeshArtifactBounds ComputeMeshArtifactBounds(const MeshArtifactData& mesh)
    {
        if (mesh.Vertices.empty()) throw std::invalid_argument("Mesh artifact requires vertices.");
        MeshArtifactBounds bounds{ mesh.Vertices.front().Position, mesh.Vertices.front().Position };
        for (const MeshArtifactVertex& vertex : mesh.Vertices)
        {
            if (!mesh_artifact_detail::IsFinite(vertex.Position))
                throw std::invalid_argument("Mesh artifact positions must be finite.");
            for (std::size_t axis = 0u; axis < 3u; ++axis)
            {
                bounds.Minimum[axis] = std::min(bounds.Minimum[axis], vertex.Position[axis]);
                bounds.Maximum[axis] = std::max(bounds.Maximum[axis], vertex.Position[axis]);
            }
        }
        return bounds;
    }

    /// Task: reject malformed, non-canonical, or numerically unsafe geometry
    /// before it reaches a cache or GPU upload. Degenerate triangles are not
    /// accepted because they produce unstable normals and acceleration bounds.
    inline void ValidateMeshArtifactData(const MeshArtifactData& mesh)
    {
        if (mesh.Vertices.size() < 3u)
            throw std::invalid_argument("Mesh artifact requires at least three vertices.");
        if (mesh.Indices.empty() || mesh.Indices.size() % 3u != 0u)
            throw std::invalid_argument("Mesh artifact indices must contain complete triangles.");
        if (mesh.Vertices.size() > std::numeric_limits<std::uint32_t>::max() ||
            mesh.Indices.size() > std::numeric_limits<std::uint32_t>::max())
            throw std::length_error("Mesh artifact exceeds 32-bit vertex or index addressing.");
        (void)mesh_artifact_detail::PayloadSize(
            mesh.Vertices.size(), mesh.Indices.size(), mesh.HasNormals, mesh.HasTexCoords);
        (void)ComputeMeshArtifactBounds(mesh);

        for (const MeshArtifactVertex& vertex : mesh.Vertices)
        {
            if (!mesh_artifact_detail::IsFinite(vertex.Normal) ||
                !mesh_artifact_detail::IsFinite(vertex.TexCoord))
                throw std::invalid_argument("Mesh artifact vertex channels must be finite.");
            if (mesh.HasNormals)
            {
                const double lengthSquared = static_cast<double>(vertex.Normal[0]) * vertex.Normal[0] +
                    static_cast<double>(vertex.Normal[1]) * vertex.Normal[1] +
                    static_cast<double>(vertex.Normal[2]) * vertex.Normal[2];
                if (std::abs(lengthSquared - 1.0) > 1.0e-3)
                    throw std::invalid_argument("Mesh artifact normals must be unit length.");
            }
            else if (vertex.Normal != std::array<float, 3u>{})
            {
                throw std::invalid_argument("Mesh artifact absent normals must be zero-filled.");
            }
            if (!mesh.HasTexCoords && vertex.TexCoord != std::array<float, 2u>{})
                throw std::invalid_argument("Mesh artifact absent texcoords must be zero-filled.");
        }

        for (std::size_t triangle = 0u; triangle < mesh.Indices.size(); triangle += 3u)
        {
            const std::uint32_t ia = mesh.Indices[triangle];
            const std::uint32_t ib = mesh.Indices[triangle + 1u];
            const std::uint32_t ic = mesh.Indices[triangle + 2u];
            if (ia >= mesh.Vertices.size() || ib >= mesh.Vertices.size() || ic >= mesh.Vertices.size())
                throw std::out_of_range("Mesh artifact index exceeds vertex count.");
            if (ia == ib || ib == ic || ia == ic)
                throw std::invalid_argument("Mesh artifact triangle repeats a vertex index.");
            const auto ab = mesh_artifact_detail::Subtract(mesh.Vertices[ib].Position, mesh.Vertices[ia].Position);
            const auto ac = mesh_artifact_detail::Subtract(mesh.Vertices[ic].Position, mesh.Vertices[ia].Position);
            const auto bc = mesh_artifact_detail::Subtract(mesh.Vertices[ic].Position, mesh.Vertices[ib].Position);
            const double maximumEdgeSquared = std::max({ mesh_artifact_detail::LengthSquared(ab),
                mesh_artifact_detail::LengthSquared(ac), mesh_artifact_detail::LengthSquared(bc) });
            const double areaSquared = mesh_artifact_detail::LengthSquared(mesh_artifact_detail::Cross(ab, ac));
            if (maximumEdgeSquared == 0.0 || areaSquared <= maximumEdgeSquared * maximumEdgeSquared * 1.0e-12)
                throw std::invalid_argument("Mesh artifact contains a degenerate triangle.");
        }
    }

    [[nodiscard]] inline std::vector<std::byte> SerializeMeshArtifactData(const MeshArtifactData& mesh)
    {
        using namespace mesh_artifact_detail;
        ValidateMeshArtifactData(mesh);
        const MeshArtifactBounds bounds = ComputeMeshArtifactBounds(mesh);
        BinaryWriter writer(PayloadSize(mesh.Vertices.size(), mesh.Indices.size(), mesh.HasNormals, mesh.HasTexCoords));
        writer.WriteBytes(Magic);
        writer.WriteU32(PayloadVersion);
        writer.WriteU32((mesh.HasNormals ? NormalsFlag : 0u) | (mesh.HasTexCoords ? TexCoordsFlag : 0u));
        writer.WriteU32(static_cast<std::uint32_t>(mesh.Vertices.size()));
        writer.WriteU32(static_cast<std::uint32_t>(mesh.Indices.size()));
        for (const float value : bounds.Minimum) writer.WriteF32(value);
        for (const float value : bounds.Maximum) writer.WriteF32(value);
        for (const MeshArtifactVertex& vertex : mesh.Vertices)
        {
            for (const float value : vertex.Position) writer.WriteF32(value);
            if (mesh.HasNormals) for (const float value : vertex.Normal) writer.WriteF32(value);
            if (mesh.HasTexCoords) for (const float value : vertex.TexCoord) writer.WriteF32(value);
        }
        for (const std::uint32_t index : mesh.Indices) writer.WriteU32(index);
        return std::move(writer).TakeBytes();
    }

    [[nodiscard]] inline MeshArtifactData ParseMeshArtifactData(std::span<const std::byte> payload)
    {
        using namespace mesh_artifact_detail;
        BinaryReader reader(payload);
        if (!std::ranges::equal(reader.ReadBytes(Magic.size()), Magic))
            throw std::invalid_argument("Mesh artifact magic is invalid.");
        if (reader.ReadU32() != PayloadVersion)
            throw std::invalid_argument("Mesh artifact payload version is unsupported.");
        const std::uint32_t flags = reader.ReadU32();
        if ((flags & ~KnownFlags) != 0u) throw std::invalid_argument("Mesh artifact flags are invalid.");
        const bool hasNormals = (flags & NormalsFlag) != 0u;
        const bool hasTexCoords = (flags & TexCoordsFlag) != 0u;
        const std::uint32_t vertexCount = reader.ReadU32();
        const std::uint32_t indexCount = reader.ReadU32();
        MeshArtifactBounds storedBounds;
        for (float& value : storedBounds.Minimum) value = reader.ReadF32();
        for (float& value : storedBounds.Maximum) value = reader.ReadF32();
        if (PayloadSize(vertexCount, indexCount, hasNormals, hasTexCoords) != payload.size())
            throw std::invalid_argument("Mesh artifact payload size does not match its counts.");

        MeshArtifactData mesh;
        mesh.HasNormals = hasNormals;
        mesh.HasTexCoords = hasTexCoords;
        mesh.Vertices.resize(vertexCount);
        mesh.Indices.resize(indexCount);
        for (MeshArtifactVertex& vertex : mesh.Vertices)
        {
            for (float& value : vertex.Position) value = reader.ReadF32();
            if (hasNormals) for (float& value : vertex.Normal) value = reader.ReadF32();
            if (hasTexCoords) for (float& value : vertex.TexCoord) value = reader.ReadF32();
        }
        for (std::uint32_t& index : mesh.Indices) index = reader.ReadU32();
        reader.RequireEnd();
        ValidateMeshArtifactData(mesh);
        if (ComputeMeshArtifactBounds(mesh) != storedBounds)
            throw std::invalid_argument("Mesh artifact stored bounds do not match its vertices.");
        return mesh;
    }

    [[nodiscard]] inline DerivedArtifact MakeMeshDerivedArtifact(const MeshArtifactData& mesh)
    {
        return { AssetType::Mesh, 1u, "kairo.mesh.v1", SerializeMeshArtifactData(mesh) };
    }

    [[nodiscard]] inline MeshArtifactData ParseMeshDerivedArtifact(const DerivedArtifact& artifact)
    {
        ValidateDerivedArtifact(artifact);
        if (artifact.Type != AssetType::Mesh || artifact.FormatVersion != 1u || artifact.Format != "kairo.mesh.v1")
            throw std::invalid_argument("Derived artifact is not a supported Kairo mesh.");
        return ParseMeshArtifactData(artifact.Payload);
    }
}
