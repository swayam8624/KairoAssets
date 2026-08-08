module;

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <limits>
#include <locale>
#include <map>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

export module Kairo.Assets.EditableMeshDocument;

import Kairo.Assets.AssetID;
import Kairo.Assets.EditableMesh;
import Kairo.Assets.ModelingOperations;
import Kairo.Assets.UVAuthoring;
import Kairo.Assets.MaterialAuthoring;
import Kairo.Assets.MaterialArtifact;
import Kairo.Assets.Types;

export namespace kairo::assets
{
    struct EditableMeshDocument final
    {
        EditableMesh Mesh;
        UVLayout UVs;
        EditableMeshModifierStack Modifiers;
        MaterialAuthoringState Materials;
    };

    namespace editable_document_detail
    {
        inline constexpr std::size_t MaximumDocumentBytes = 64u * 1024u * 1024u;

        [[nodiscard]] inline char HexDigit(std::uint8_t value) noexcept
        {
            return value < 10u ? static_cast<char>('0' + value)
                : static_cast<char>('a' + (value - 10u));
        }

        [[nodiscard]] inline std::uint8_t HexValue(char value)
        {
            if (value >= '0' && value <= '9') return static_cast<std::uint8_t>(value - '0');
            if (value >= 'a' && value <= 'f') return static_cast<std::uint8_t>(value - 'a' + 10);
            if (value >= 'A' && value <= 'F') return static_cast<std::uint8_t>(value - 'A' + 10);
            throw std::invalid_argument("Editable mesh document contains invalid hexadecimal data.");
        }

        [[nodiscard]] inline std::string EncodeBytes(const std::vector<std::byte>& bytes)
        {
            std::string result;
            result.reserve(bytes.size() * 2u);
            for (const std::byte byte : bytes)
            {
                const auto value = std::to_integer<std::uint8_t>(byte);
                result.push_back(HexDigit(static_cast<std::uint8_t>(value >> 4u)));
                result.push_back(HexDigit(static_cast<std::uint8_t>(value & 0x0fu)));
            }
            return result;
        }

        [[nodiscard]] inline std::vector<std::byte> DecodeBytes(std::string_view text)
        {
            if ((text.size() % 2u) != 0u)
                throw std::invalid_argument("Editable mesh document hexadecimal payload has odd length.");
            std::vector<std::byte> result;
            result.reserve(text.size() / 2u);
            for (std::size_t i = 0u; i < text.size(); i += 2u)
            {
                const auto value = static_cast<std::uint8_t>(
                    (HexValue(text[i]) << 4u) | HexValue(text[i + 1u]));
                result.push_back(static_cast<std::byte>(value));
            }
            return result;
        }

        inline void RequireToken(std::istream& input, std::string_view expected)
        {
            std::string actual;
            if (!(input >> actual) || actual != expected)
                throw std::invalid_argument("Editable mesh document expected token '" +
                    std::string(expected) + "'.");
        }

        template<class T>
        [[nodiscard]] T Read(std::istream& input, std::string_view field)
        {
            T value{};
            if (!(input >> value))
                throw std::invalid_argument("Editable mesh document could not parse " + std::string(field) + ".");
            return value;
        }

        [[nodiscard]] inline std::uint8_t EnumByte(std::uint8_t value) noexcept { return value; }
    }

    [[nodiscard]] inline std::string SerializeEditableMeshDocument(
        const EditableMeshDocument& document)
    {
        using namespace editable_document_detail;
        const auto validation = document.Mesh.Validate();
        if (!validation.Valid) throw std::invalid_argument(validation.Errors.front());

        std::ostringstream output;
        output.imbue(std::locale::classic());
        output << std::setprecision(std::numeric_limits<double>::max_digits10);
        output << "kairo.editable-mesh.v1\n";

        std::map<EditableVertexID, std::size_t> vertexIndices;
        output << "vertices " << document.Mesh.Vertices().size() << '\n';
        std::size_t vertexIndex = 0u;
        for (const auto& [id, vertex] : document.Mesh.Vertices())
        {
            vertexIndices.emplace(id, vertexIndex++);
            output << "v " << vertex.Position[0] << ' ' << vertex.Position[1] << ' '
                << vertex.Position[2] << '\n';
        }

        std::map<EditableFaceID, std::size_t> faceIndices;
        output << "faces " << document.Mesh.Faces().size() << '\n';
        std::size_t faceIndex = 0u;
        for (const auto& [id, face] : document.Mesh.Faces())
        {
            faceIndices.emplace(id, faceIndex++);
            output << "f " << face.MaterialSlot << ' ' << face.Vertices.size();
            for (const auto vertex : face.Vertices) output << ' ' << vertexIndices.at(vertex);
            output << '\n';
        }

        output << "uvs " << document.UVs.Coordinates().size() << '\n';
        for (const auto& [corner, uv] : document.UVs.Coordinates())
            output << "u " << faceIndices.at(corner.Face) << ' ' << corner.Corner << ' '
                << uv[0] << ' ' << uv[1] << '\n';

        output << "seams " << document.UVs.Seams().size() << '\n';
        for (const auto seam : document.UVs.Seams())
            output << "s " << vertexIndices.at(seam.A) << ' ' << vertexIndices.at(seam.B) << '\n';

        output << "modifiers " << document.Modifiers.Modifiers().size() << '\n';
        for (const auto& modifier : document.Modifiers.Modifiers())
        {
            std::visit([&](const auto& value) {
                using T = std::decay_t<decltype(value)>;
                if constexpr (std::is_same_v<T, TranslateModifier>)
                    output << "m translate " << value.Offset[0] << ' ' << value.Offset[1] << ' '
                        << value.Offset[2] << '\n';
                else if constexpr (std::is_same_v<T, TriangulateModifier>)
                    output << "m triangulate\n";
                else if constexpr (std::is_same_v<T, SubdivideModifier>)
                    output << "m subdivide " << value.Levels << '\n';
            }, modifier);
        }

        output << "materials " << document.Materials.Materials().size() << '\n';
        for (const auto& material : document.Materials.Materials())
            output << "mat " << EncodeBytes(SerializeMaterialArtifactData(material)) << '\n';

        output << "textures " << document.Materials.TextureBindings().size() << '\n';
        for (const auto& binding : document.Materials.TextureBindings())
        {
            binding.Settings.Validate();
            output << "tex " << binding.Texture.ID.ToString() << ' '
                << static_cast<unsigned>(binding.Settings.ColorSpace) << ' '
                << static_cast<unsigned>(binding.Settings.Semantic) << ' '
                << static_cast<unsigned>(binding.Settings.Mips) << ' '
                << binding.Settings.MaximumResolution << ' '
                << (binding.Settings.FlipVertical ? 1u : 0u) << '\n';
        }
        output << "end\n";
        const std::string result = output.str();
        if (result.size() > MaximumDocumentBytes)
            throw std::length_error("Editable mesh document exceeds its 64 MiB text safety limit.");
        return result;
    }

    [[nodiscard]] inline EditableMeshDocument ParseEditableMeshDocument(std::string_view text)
    {
        using namespace editable_document_detail;
        if (text.size() > MaximumDocumentBytes)
            throw std::length_error("Editable mesh document exceeds its 64 MiB text safety limit.");
        std::istringstream input{ std::string(text) };
        input.imbue(std::locale::classic());
        RequireToken(input, "kairo.editable-mesh.v1");

        EditableMeshDocument document;
        RequireToken(input, "vertices");
        const std::size_t vertexCount = Read<std::size_t>(input, "vertex count");
        if (vertexCount > EditableMesh::MaximumVertices)
            throw std::length_error("Editable mesh document exceeds the vertex safety limit.");
        std::vector<EditableVertexID> vertices;
        vertices.reserve(vertexCount);
        for (std::size_t i = 0u; i < vertexCount; ++i)
        {
            RequireToken(input, "v");
            const double x = Read<double>(input, "vertex x");
            const double y = Read<double>(input, "vertex y");
            const double z = Read<double>(input, "vertex z");
            vertices.push_back(document.Mesh.AddVertex({ x, y, z }));
        }

        RequireToken(input, "faces");
        const std::size_t faceCount = Read<std::size_t>(input, "face count");
        if (faceCount > EditableMesh::MaximumFaces)
            throw std::length_error("Editable mesh document exceeds the face safety limit.");
        std::vector<EditableFaceID> faces;
        faces.reserve(faceCount);
        for (std::size_t i = 0u; i < faceCount; ++i)
        {
            RequireToken(input, "f");
            const std::uint32_t materialSlot = Read<std::uint32_t>(input, "material slot");
            const std::size_t count = Read<std::size_t>(input, "face vertex count");
            if (count < 3u || count > vertexCount)
                throw std::invalid_argument("Editable mesh document face vertex count is invalid.");
            std::vector<EditableVertexID> faceVertices;
            faceVertices.reserve(count);
            for (std::size_t corner = 0u; corner < count; ++corner)
            {
                const std::size_t index = Read<std::size_t>(input, "face vertex index");
                if (index >= vertices.size())
                    throw std::out_of_range("Editable mesh document face vertex index is out of range.");
                faceVertices.push_back(vertices[index]);
            }
            faces.push_back(document.Mesh.AddFace(std::move(faceVertices), materialSlot));
        }

        RequireToken(input, "uvs");
        const std::size_t uvCount = Read<std::size_t>(input, "UV count");
        for (std::size_t i = 0u; i < uvCount; ++i)
        {
            RequireToken(input, "u");
            const std::size_t faceIndex = Read<std::size_t>(input, "UV face index");
            const std::size_t cornerIndex = Read<std::size_t>(input, "UV corner index");
            const double u = Read<double>(input, "UV u");
            const double v = Read<double>(input, "UV v");
            if (faceIndex >= faces.size() ||
                cornerIndex >= document.Mesh.Face(faces[faceIndex]).Vertices.size())
                throw std::out_of_range("Editable mesh document UV corner is out of range.");
            document.UVs.Set({ faces[faceIndex], cornerIndex }, { u, v });
        }

        RequireToken(input, "seams");
        const std::size_t seamCount = Read<std::size_t>(input, "seam count");
        for (std::size_t i = 0u; i < seamCount; ++i)
        {
            RequireToken(input, "s");
            const std::size_t a = Read<std::size_t>(input, "seam vertex a");
            const std::size_t b = Read<std::size_t>(input, "seam vertex b");
            if (a >= vertices.size() || b >= vertices.size())
                throw std::out_of_range("Editable mesh document seam vertex is out of range.");
            document.UVs.MarkSeam(EditableEdgeKey::Canonical(vertices[a], vertices[b]));
        }

        RequireToken(input, "modifiers");
        const std::size_t modifierCount = Read<std::size_t>(input, "modifier count");
        if (modifierCount > EditableMeshModifierStack::MaximumModifiers)
            throw std::length_error("Editable mesh document exceeds the modifier safety limit.");
        for (std::size_t i = 0u; i < modifierCount; ++i)
        {
            RequireToken(input, "m");
            const std::string kind = Read<std::string>(input, "modifier kind");
            if (kind == "translate")
            {
                TranslateModifier modifier;
                modifier.Offset[0] = Read<double>(input, "translate x");
                modifier.Offset[1] = Read<double>(input, "translate y");
                modifier.Offset[2] = Read<double>(input, "translate z");
                document.Modifiers.Add(modifier);
            }
            else if (kind == "triangulate") document.Modifiers.Add(TriangulateModifier{});
            else if (kind == "subdivide")
                document.Modifiers.Add(SubdivideModifier{ Read<std::uint32_t>(input, "subdivision levels") });
            else throw std::invalid_argument("Editable mesh document contains an unknown modifier.");
        }

        RequireToken(input, "materials");
        const std::size_t materialCount = Read<std::size_t>(input, "material count");
        if (materialCount > MaterialAuthoringState::MaximumMaterials)
            throw std::length_error("Editable mesh document exceeds the material safety limit.");
        for (std::size_t i = 0u; i < materialCount; ++i)
        {
            RequireToken(input, "mat");
            const std::string payload = Read<std::string>(input, "material payload");
            const auto bytes = DecodeBytes(payload);
            (void)document.Materials.AddMaterial(ParseMaterialArtifactData(bytes));
        }

        RequireToken(input, "textures");
        const std::size_t textureCount = Read<std::size_t>(input, "texture setting count");
        if (textureCount > MaterialAuthoringState::MaximumTextureBindings)
            throw std::length_error("Editable mesh document exceeds the texture-setting safety limit.");
        for (std::size_t i = 0u; i < textureCount; ++i)
        {
            RequireToken(input, "tex");
            const AssetID id = AssetID::Parse(Read<std::string>(input, "texture asset ID"));
            TextureAuthoringSettings settings;
            settings.ColorSpace = static_cast<TextureColorSpace>(Read<unsigned>(input, "texture color space"));
            settings.Semantic = static_cast<TextureSemantic>(Read<unsigned>(input, "texture semantic"));
            settings.Mips = static_cast<TextureMipPolicy>(Read<unsigned>(input, "texture mip policy"));
            settings.MaximumResolution = Read<std::uint32_t>(input, "texture maximum resolution");
            const unsigned flip = Read<unsigned>(input, "texture flip flag");
            if (flip > 1u) throw std::invalid_argument("Editable mesh document texture flip flag is invalid.");
            settings.FlipVertical = flip != 0u;
            settings.Validate();
            document.Materials.BindTexture(TextureAssetHandle{ id }, settings);
        }

        RequireToken(input, "end");
        std::string trailing;
        if (input >> trailing)
            throw std::invalid_argument("Editable mesh document contains trailing data.");
        return document;
    }

    inline void SaveEditableMeshDocument(
        const EditableMeshDocument& document,
        const std::filesystem::path& path)
    {
        if (path.empty()) throw std::invalid_argument("Editable mesh document path cannot be empty.");
        if (path.has_parent_path()) std::filesystem::create_directories(path.parent_path());
        const std::string text = SerializeEditableMeshDocument(document);
        std::ofstream output(path, std::ios::binary | std::ios::trunc);
        if (!output) throw std::runtime_error("Failed to open editable mesh document for writing.");
        output.write(text.data(), static_cast<std::streamsize>(text.size()));
        if (!output) throw std::runtime_error("Failed while writing editable mesh document.");
    }

    [[nodiscard]] inline EditableMeshDocument LoadEditableMeshDocument(
        const std::filesystem::path& path)
    {
        using namespace editable_document_detail;
        std::ifstream input(path, std::ios::binary);
        if (!input) throw std::runtime_error("Failed to open editable mesh document.");
        input.seekg(0, std::ios::end);
        const auto end = input.tellg();
        if (end < 0 || static_cast<std::uint64_t>(end) > MaximumDocumentBytes)
            throw std::length_error("Editable mesh document exceeds its 64 MiB safety limit.");
        input.seekg(0, std::ios::beg);
        std::string text(static_cast<std::size_t>(end), '\0');
        input.read(text.data(), static_cast<std::streamsize>(text.size()));
        if (!input && !text.empty()) throw std::runtime_error("Failed while reading editable mesh document.");
        return ParseEditableMeshDocument(text);
    }
}
