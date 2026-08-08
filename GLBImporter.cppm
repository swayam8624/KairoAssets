module;

#include <algorithm>
#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>
#include <nlohmann/json.hpp>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

export module Kairo.Assets.GLBImporter;

import Kairo.Assets.DerivedArtifact;
import Kairo.Assets.Importer;
import Kairo.Assets.MeshArtifact;
import Kairo.Assets.Types;

export namespace kairo::assets
{
    namespace glb_importer_detail
    {
        constexpr std::uint32_t Magic = 0x46546c67u;
        constexpr std::uint32_t Version = 2u;
        constexpr std::uint32_t JsonChunk = 0x4e4f534au;
        constexpr std::uint32_t BinChunk = 0x004e4942u;

        [[nodiscard]] inline std::uint32_t ReadU32(
            std::span<const std::byte> bytes, std::size_t offset)
        {
            if (offset > bytes.size() || bytes.size() - offset < 4u)
                throw std::invalid_argument("GLB input is truncated.");
            std::uint32_t value = 0u;
            for (std::size_t index = 0u; index < 4u; ++index)
                value |= std::uint32_t(std::to_integer<std::uint8_t>(bytes[offset + index])) << (index * 8u);
            return value;
        }

        struct Document final
        {
            nlohmann::json Json;
            std::span<const std::byte> Binary;
        };

        [[nodiscard]] inline Document ParseContainer(std::span<const std::byte> source)
        {
            if (source.size() < 20u) throw std::invalid_argument("GLB input is too small.");
            if (ReadU32(source, 0u) != Magic) throw std::invalid_argument("GLB magic is invalid.");
            if (ReadU32(source, 4u) != Version) throw std::invalid_argument("Only GLB version 2 is supported.");
            if (ReadU32(source, 8u) != source.size())
                throw std::invalid_argument("GLB declared length does not match the source size.");

            std::size_t cursor = 12u;
            std::span<const std::byte> jsonBytes;
            std::span<const std::byte> binaryBytes;
            while (cursor < source.size())
            {
                const std::uint32_t length = ReadU32(source, cursor);
                const std::uint32_t type = ReadU32(source, cursor + 4u);
                cursor += 8u;
                if (length > source.size() - cursor)
                    throw std::invalid_argument("GLB chunk exceeds the declared container length.");
                const auto chunk = source.subspan(cursor, length);
                cursor += length;
                if (type == JsonChunk)
                {
                    if (!jsonBytes.empty()) throw std::invalid_argument("GLB contains multiple JSON chunks.");
                    jsonBytes = chunk;
                }
                else if (type == BinChunk)
                {
                    if (!binaryBytes.empty()) throw std::invalid_argument("GLB contains multiple BIN chunks.");
                    binaryBytes = chunk;
                }
            }
            if (jsonBytes.empty()) throw std::invalid_argument("GLB requires a JSON chunk.");
            std::string jsonText(jsonBytes.size(), '\0');
            for (std::size_t index = 0u; index < jsonBytes.size(); ++index)
                jsonText[index] = static_cast<char>(std::to_integer<unsigned char>(jsonBytes[index]));
            while (!jsonText.empty() && (jsonText.back() == ' ' || jsonText.back() == '\0'))
                jsonText.pop_back();
            Document result;
            result.Json = nlohmann::json::parse(jsonText);
            result.Binary = binaryBytes;
            return result;
        }

        [[nodiscard]] inline std::size_t ComponentBytes(std::uint32_t componentType)
        {
            switch (componentType)
            {
                case 5121u: return 1u;
                case 5123u: return 2u;
                case 5125u:
                case 5126u: return 4u;
                default: throw std::invalid_argument("GLB accessor component type is unsupported.");
            }
        }

        [[nodiscard]] inline std::size_t TypeComponents(std::string_view type)
        {
            if (type == "SCALAR") return 1u;
            if (type == "VEC2") return 2u;
            if (type == "VEC3") return 3u;
            throw std::invalid_argument("GLB accessor type is unsupported for static mesh import.");
        }

        struct AccessorView final
        {
            std::span<const std::byte> Bytes;
            std::size_t Count = 0u;
            std::size_t Stride = 0u;
            std::uint32_t ComponentType = 0u;
            std::size_t Components = 0u;
        };

        [[nodiscard]] inline AccessorView Accessor(const Document& document, std::size_t index)
        {
            const auto& accessors = document.Json.at("accessors");
            const auto& views = document.Json.at("bufferViews");
            if (index >= accessors.size()) throw std::out_of_range("GLB accessor index is invalid.");
            const auto& accessor = accessors.at(index);
            if (accessor.contains("sparse")) throw std::invalid_argument("Sparse GLB accessors are not supported.");
            const std::size_t viewIndex = accessor.at("bufferView").get<std::size_t>();
            if (viewIndex >= views.size()) throw std::out_of_range("GLB bufferView index is invalid.");
            const auto& view = views.at(viewIndex);
            if (view.value("buffer", 0u) != 0u) throw std::invalid_argument("GLB may reference only its embedded buffer.");

            const std::uint32_t componentType = accessor.at("componentType").get<std::uint32_t>();
            const std::size_t components = TypeComponents(accessor.at("type").get<std::string>());
            const std::size_t packed = ComponentBytes(componentType) * components;
            const std::size_t stride = view.value("byteStride", packed);
            if (stride < packed || stride > 252u)
                throw std::invalid_argument("GLB accessor stride is invalid.");
            const std::size_t count = accessor.at("count").get<std::size_t>();
            const std::size_t viewOffset = view.value("byteOffset", 0u);
            const std::size_t accessorOffset = accessor.value("byteOffset", 0u);
            const std::size_t viewLength = view.at("byteLength").get<std::size_t>();
            if (viewOffset > document.Binary.size() || viewLength > document.Binary.size() - viewOffset)
                throw std::invalid_argument("GLB bufferView exceeds the BIN chunk.");
            if (accessorOffset > viewLength)
                throw std::invalid_argument("GLB accessor offset exceeds its bufferView.");
            const std::size_t required = count == 0u ? 0u : (count - 1u) * stride + packed;
            if (required > viewLength - accessorOffset)
                throw std::invalid_argument("GLB accessor exceeds its bufferView.");
            return { document.Binary.subspan(viewOffset + accessorOffset, required),
                count, stride, componentType, components };
        }

        [[nodiscard]] inline float ReadFloat(const AccessorView& view,
            std::size_t element, std::size_t component)
        {
            if (view.ComponentType != 5126u) throw std::invalid_argument("GLB vertex channels must use FLOAT components.");
            if (element >= view.Count || component >= view.Components) throw std::out_of_range("GLB vertex channel read is invalid.");
            const std::size_t offset = element * view.Stride + component * sizeof(float);
            std::uint32_t bits = 0u;
            for (std::size_t index = 0u; index < 4u; ++index)
                bits |= std::uint32_t(std::to_integer<std::uint8_t>(view.Bytes[offset + index])) << (index * 8u);
            return std::bit_cast<float>(bits);
        }

        [[nodiscard]] inline std::uint32_t ReadIndex(const AccessorView& view, std::size_t element)
        {
            if (view.Components != 1u || element >= view.Count)
                throw std::invalid_argument("GLB index accessor must be scalar.");
            const std::size_t offset = element * view.Stride;
            switch (view.ComponentType)
            {
                case 5121u: return std::to_integer<std::uint8_t>(view.Bytes[offset]);
                case 5123u:
                    return std::uint32_t(std::to_integer<std::uint8_t>(view.Bytes[offset])) |
                        (std::uint32_t(std::to_integer<std::uint8_t>(view.Bytes[offset + 1u])) << 8u);
                case 5125u:
                {
                    std::uint32_t value = 0u;
                    for (std::size_t index = 0u; index < 4u; ++index)
                        value |= std::uint32_t(std::to_integer<std::uint8_t>(view.Bytes[offset + index])) << (index * 8u);
                    return value;
                }
                default: throw std::invalid_argument("GLB index component type is unsupported.");
            }
        }

        inline void AppendPrimitive(const Document& document, const nlohmann::json& primitive,
            MeshArtifactData& mesh)
        {
            if (primitive.value("mode", 4u) != 4u)
                throw std::invalid_argument("Only GLB triangle-list primitives are supported.");
            if (!primitive.contains("indices"))
                throw std::invalid_argument("Static GLB primitives require indices.");
            const auto& attributes = primitive.at("attributes");
            if (!attributes.contains("POSITION"))
                throw std::invalid_argument("Static GLB primitives require POSITION.");

            const AccessorView positions = Accessor(document, attributes.at("POSITION").get<std::size_t>());
            if (positions.ComponentType != 5126u || positions.Components != 3u || positions.Count < 3u)
                throw std::invalid_argument("GLB POSITION must be a FLOAT VEC3 accessor with at least three vertices.");
            std::optional<AccessorView> normals;
            std::optional<AccessorView> texcoords;
            if (attributes.contains("NORMAL"))
            {
                normals = Accessor(document, attributes.at("NORMAL").get<std::size_t>());
                if (normals->ComponentType != 5126u || normals->Components != 3u || normals->Count != positions.Count)
                    throw std::invalid_argument("GLB NORMAL must be a matching FLOAT VEC3 accessor.");
            }
            if (attributes.contains("TEXCOORD_0"))
            {
                texcoords = Accessor(document, attributes.at("TEXCOORD_0").get<std::size_t>());
                if (texcoords->ComponentType != 5126u || texcoords->Components != 2u || texcoords->Count != positions.Count)
                    throw std::invalid_argument("GLB TEXCOORD_0 must be a matching FLOAT VEC2 accessor.");
            }
            if (!mesh.Vertices.empty() && (mesh.HasNormals != normals.has_value() || mesh.HasTexCoords != texcoords.has_value()))
                throw std::invalid_argument("All flattened GLB primitives must expose the same vertex channels.");
            mesh.HasNormals = normals.has_value();
            mesh.HasTexCoords = texcoords.has_value();
            const std::uint32_t baseVertex = static_cast<std::uint32_t>(mesh.Vertices.size());
            if (positions.Count > std::numeric_limits<std::uint32_t>::max() - mesh.Vertices.size())
                throw std::length_error("Flattened GLB mesh exceeds 32-bit vertex addressing.");
            for (std::size_t vertexIndex = 0u; vertexIndex < positions.Count; ++vertexIndex)
            {
                MeshArtifactVertex vertex;
                for (std::size_t axis = 0u; axis < 3u; ++axis)
                    vertex.Position[axis] = ReadFloat(positions, vertexIndex, axis);
                if (normals.has_value())
                    for (std::size_t axis = 0u; axis < 3u; ++axis)
                        vertex.Normal[axis] = ReadFloat(*normals, vertexIndex, axis);
                if (texcoords.has_value())
                    for (std::size_t axis = 0u; axis < 2u; ++axis)
                        vertex.TexCoord[axis] = ReadFloat(*texcoords, vertexIndex, axis);
                mesh.Vertices.push_back(vertex);
            }

            const AccessorView indices = Accessor(document, primitive.at("indices").get<std::size_t>());
            if (indices.Count == 0u || indices.Count % 3u != 0u)
                throw std::invalid_argument("GLB index accessor must contain complete triangles.");
            for (std::size_t index = 0u; index < indices.Count; ++index)
            {
                const std::uint32_t local = ReadIndex(indices, index);
                if (local >= positions.Count) throw std::out_of_range("GLB index exceeds its primitive vertex count.");
                mesh.Indices.push_back(baseVertex + local);
            }
        }
    }

    class StaticGLBImporter final : public AssetImporter
    {
    public:
        [[nodiscard]] std::string Identifier() const override { return "kairo.gltf.static"; }
        [[nodiscard]] std::string Version() const override { return "1"; }

        [[nodiscard]] DerivedArtifact Import(const ImportRequest& request) const override
        {
            if (request.ExpectedType != AssetType::Mesh)
                throw std::invalid_argument("Static GLB importer produces mesh assets only.");
            const auto document = glb_importer_detail::ParseContainer(request.SourceBytes);
            if (!document.Json.contains("asset") || document.Json.at("asset").value("version", "") != "2.0")
                throw std::invalid_argument("GLB JSON must declare glTF asset version 2.0.");
            if (!document.Json.contains("meshes")) throw std::invalid_argument("GLB contains no meshes.");

            MeshArtifactData mesh;
            for (const auto& sourceMesh : document.Json.at("meshes"))
            {
                if (!sourceMesh.contains("primitives")) throw std::invalid_argument("GLB mesh has no primitives.");
                for (const auto& primitive : sourceMesh.at("primitives"))
                    glb_importer_detail::AppendPrimitive(document, primitive, mesh);
            }
            ValidateMeshArtifactData(mesh);
            return MakeMeshDerivedArtifact(mesh);
        }
    };
}
