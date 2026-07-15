module;

#include <algorithm>
#include <array>
#include <charconv>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <numeric>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <system_error>
#include <unordered_map>
#include <utility>
#include <vector>

export module Kairo.Assets.OBJImporter;

import Kairo.Assets.Importer;
import Kairo.Assets.DerivedArtifact;
import Kairo.Assets.MeshArtifact;
import Kairo.Assets.Types;

export namespace kairo::assets
{
    /// Source-located OBJ diagnostic. Line and Column are one-based and point
    /// to the command or token that made the input unrecoverable.
    class OBJImportError final : public std::runtime_error
    {
    public:
        OBJImportError(std::size_t line, std::size_t column, std::string message)
            : std::runtime_error("OBJ line " + std::to_string(line) + ", column " +
                std::to_string(column) + ": " + message), Line(line), Column(column) {}

        std::size_t Line;
        std::size_t Column;
    };

    namespace obj_import_detail
    {
        struct Token final { std::string_view Text; std::size_t Column = 1u; };
        struct Corner final
        {
            std::uint32_t Position = 0u;
            std::int32_t TexCoord = -1;
            std::int32_t Normal = -1;
            std::size_t Column = 1u;
        };
        struct TriangleCorners final { Corner A; Corner B; Corner C; };

        struct VertexKey final
        {
            std::uint32_t Position = 0u;
            std::int32_t TexCoord = -1;
            std::int32_t Normal = -1;
            std::uint64_t Smoothing = 0u;
            friend bool operator==(const VertexKey&, const VertexKey&) = default;
        };

        struct VertexKeyHash final
        {
            [[nodiscard]] std::size_t operator()(const VertexKey& key) const noexcept
            {
                std::size_t value = key.Position;
                const auto combine = [&value](std::uint64_t part)
                {
                    value ^= static_cast<std::size_t>(part) + 0x9e3779b97f4a7c15ull + (value << 6u) + (value >> 2u);
                };
                combine(static_cast<std::uint32_t>(key.TexCoord));
                combine(static_cast<std::uint32_t>(key.Normal));
                combine(key.Smoothing);
                return value;
            }
        };

        [[noreturn]] inline void Fail(std::size_t line, std::size_t column, std::string message)
        {
            throw OBJImportError(line, column, std::move(message));
        }

        [[nodiscard]] inline std::vector<Token> Tokenize(std::string_view line)
        {
            std::vector<Token> result;
            std::size_t cursor = 0u;
            while (cursor < line.size())
            {
                while (cursor < line.size() && (line[cursor] == ' ' || line[cursor] == '\t')) ++cursor;
                if (cursor == line.size() || line[cursor] == '#') break;
                const std::size_t start = cursor;
                while (cursor < line.size() && line[cursor] != ' ' && line[cursor] != '\t' && line[cursor] != '#') ++cursor;
                result.push_back({ line.substr(start, cursor - start), start + 1u });
                if (cursor < line.size() && line[cursor] == '#') break;
            }
            return result;
        }

        [[nodiscard]] inline double ParseNumber(const Token& token, std::size_t line, std::string_view label)
        {
            double value = 0.0;
            const char* begin = token.Text.data();
            const char* end = begin + token.Text.size();
            const auto parsed = std::from_chars(begin, end, value, std::chars_format::general);
            if (parsed.ec != std::errc{} || parsed.ptr != end || !std::isfinite(value))
                Fail(line, token.Column, "invalid finite " + std::string(label) + ".");
            return value;
        }

        [[nodiscard]] inline std::int64_t ParseIndex(std::string_view text,
            std::size_t line, std::size_t column, std::string_view label)
        {
            if (text.empty()) Fail(line, column, "missing " + std::string(label) + " index.");
            std::int64_t value = 0;
            const char* begin = text.data();
            const char* end = begin + text.size();
            const auto parsed = std::from_chars(begin, end, value);
            if (parsed.ec != std::errc{} || parsed.ptr != end)
                Fail(line, column, "invalid " + std::string(label) + " index.");
            if (value == 0) Fail(line, column, std::string(label) + " indices are 1-based; zero is invalid.");
            return value;
        }

        [[nodiscard]] inline std::uint32_t ResolveIndex(std::int64_t index, std::size_t count,
            std::size_t line, std::size_t column, std::string_view label)
        {
            const std::int64_t resolved = index > 0 ? index - 1 : static_cast<std::int64_t>(count) + index;
            if (resolved < 0 || static_cast<std::uint64_t>(resolved) >= count)
                Fail(line, column, std::string(label) + " index is out of range.");
            return static_cast<std::uint32_t>(resolved);
        }

        [[nodiscard]] inline Corner ParseCorner(const Token& token, std::size_t line,
            std::size_t positionCount, std::size_t texCoordCount, std::size_t normalCount)
        {
            const std::size_t first = token.Text.find('/');
            const std::size_t second = first == std::string_view::npos
                ? std::string_view::npos : token.Text.find('/', first + 1u);
            if (second != std::string_view::npos && token.Text.find('/', second + 1u) != std::string_view::npos)
                Fail(line, token.Column, "face vertex contains too many separators.");
            const std::string_view positionText = first == std::string_view::npos
                ? token.Text : token.Text.substr(0u, first);
            const std::string_view texCoordText = first == std::string_view::npos ? std::string_view{} :
                (second == std::string_view::npos ? token.Text.substr(first + 1u) :
                    token.Text.substr(first + 1u, second - first - 1u));
            const std::string_view normalText = second == std::string_view::npos
                ? std::string_view{} : token.Text.substr(second + 1u);

            Corner result;
            result.Column = token.Column;
            result.Position = ResolveIndex(ParseIndex(positionText, line, token.Column, "position"),
                positionCount, line, token.Column, "position");
            if (!texCoordText.empty())
                result.TexCoord = static_cast<std::int32_t>(ResolveIndex(
                    ParseIndex(texCoordText, line, token.Column + first + 1u, "texcoord"),
                    texCoordCount, line, token.Column + first + 1u, "texcoord"));
            if (!normalText.empty())
                result.Normal = static_cast<std::int32_t>(ResolveIndex(
                    ParseIndex(normalText, line, token.Column + second + 1u, "normal"),
                    normalCount, line, token.Column + second + 1u, "normal"));
            return result;
        }

        [[nodiscard]] inline std::array<double, 3u> Subtract(
            const std::array<float, 3u>& left, const std::array<float, 3u>& right) noexcept
        {
            return { static_cast<double>(left[0]) - right[0], static_cast<double>(left[1]) - right[1],
                static_cast<double>(left[2]) - right[2] };
        }

        [[nodiscard]] inline std::array<double, 3u> Cross(
            const std::array<double, 3u>& left, const std::array<double, 3u>& right) noexcept
        {
            return { left[1] * right[2] - left[2] * right[1], left[2] * right[0] - left[0] * right[2],
                left[0] * right[1] - left[1] * right[0] };
        }

        [[nodiscard]] inline double LengthSquared(const std::array<double, 3u>& value) noexcept
        { return value[0] * value[0] + value[1] * value[1] + value[2] * value[2]; }

        [[nodiscard]] inline std::array<float, 3u> Normalize(const std::array<double, 3u>& value,
            std::size_t line, std::size_t column, std::string_view label)
        {
            const double lengthSquared = LengthSquared(value);
            if (!std::isfinite(lengthSquared) || lengthSquared <= 1.0e-30)
                Fail(line, column, std::string(label) + " has zero or unstable length.");
            const double inverseLength = 1.0 / std::sqrt(lengthSquared);
            return { static_cast<float>(value[0] * inverseLength), static_cast<float>(value[1] * inverseLength),
                static_cast<float>(value[2] * inverseLength) };
        }

        [[nodiscard]] inline std::vector<TriangleCorners> Triangulate(const std::vector<Corner>& corners,
            const std::vector<std::array<float, 3u>>& positions, std::size_t line)
        {
            if (corners.size() == 3u) return { { corners[0], corners[1], corners[2] } };
            std::array<double, 3u> normal{};
            std::array<double, 3u> minimum{
                positions[corners.front().Position][0], positions[corners.front().Position][1],
                positions[corners.front().Position][2] };
            std::array<double, 3u> maximum = minimum;
            for (std::size_t index = 0u; index < corners.size(); ++index)
            {
                const auto& current = positions[corners[index].Position];
                const auto& next = positions[corners[(index + 1u) % corners.size()].Position];
                normal[0] += (static_cast<double>(current[1]) - next[1]) * (static_cast<double>(current[2]) + next[2]);
                normal[1] += (static_cast<double>(current[2]) - next[2]) * (static_cast<double>(current[0]) + next[0]);
                normal[2] += (static_cast<double>(current[0]) - next[0]) * (static_cast<double>(current[1]) + next[1]);
                for (std::size_t axis = 0u; axis < 3u; ++axis)
                {
                    minimum[axis] = std::min(minimum[axis], static_cast<double>(current[axis]));
                    maximum[axis] = std::max(maximum[axis], static_cast<double>(current[axis]));
                }
            }
            const double scale = std::max({ maximum[0] - minimum[0], maximum[1] - minimum[1],
                maximum[2] - minimum[2] });
            if (LengthSquared(normal) <= std::max(1.0, scale * scale * scale * scale) * 1.0e-24)
                Fail(line, corners.front().Column, "polygon has zero projected area.");
            const double normalLength = std::sqrt(LengthSquared(normal));
            const auto& origin = positions[corners.front().Position];
            double maximumExtent = 0.0;
            for (const Corner& corner : corners)
            {
                const auto offset = Subtract(positions[corner.Position], origin);
                maximumExtent = std::max(maximumExtent, std::sqrt(LengthSquared(offset)));
                const double distance = std::abs(offset[0] * normal[0] + offset[1] * normal[1] +
                    offset[2] * normal[2]) / normalLength;
                if (distance > std::max(1.0, maximumExtent) * 1.0e-5)
                    Fail(line, corner.Column, "polygon vertices are not coplanar.");
            }

            const std::size_t droppedAxis = std::abs(normal[0]) > std::abs(normal[1])
                ? (std::abs(normal[0]) > std::abs(normal[2]) ? 0u : 2u)
                : (std::abs(normal[1]) > std::abs(normal[2]) ? 1u : 2u);
            const auto project = [droppedAxis](const std::array<float, 3u>& point)
            {
                if (droppedAxis == 0u) return std::array<double, 2u>{ point[1], point[2] };
                if (droppedAxis == 1u) return std::array<double, 2u>{ point[0], point[2] };
                return std::array<double, 2u>{ point[0], point[1] };
            };
            std::vector<std::array<double, 2u>> points;
            points.reserve(corners.size());
            for (const Corner& corner : corners) points.push_back(project(positions[corner.Position]));
            const auto cross2 = [](const auto& a, const auto& b, const auto& c)
            { return (b[0] - a[0]) * (c[1] - a[1]) - (b[1] - a[1]) * (c[0] - a[0]); };
            double signedArea = 0.0;
            for (std::size_t index = 0u; index < points.size(); ++index)
            {
                const auto& a = points[index]; const auto& b = points[(index + 1u) % points.size()];
                signedArea += a[0] * b[1] - b[0] * a[1];
            }
            const double orientation = signedArea > 0.0 ? 1.0 : -1.0;
            const double epsilon = std::max(1.0, scale * scale) * 1.0e-12;
            const auto inside = [cross2, orientation, epsilon](const auto& point, const auto& a, const auto& b, const auto& c)
            {
                return orientation * cross2(a, b, point) >= -epsilon &&
                    orientation * cross2(b, c, point) >= -epsilon &&
                    orientation * cross2(c, a, point) >= -epsilon;
            };

            std::vector<std::size_t> remaining(corners.size());
            std::iota(remaining.begin(), remaining.end(), 0u);
            std::vector<TriangleCorners> triangles;
            triangles.reserve(corners.size() - 2u);
            while (remaining.size() > 3u)
            {
                bool clipped = false;
                for (std::size_t candidate = 0u; candidate < remaining.size(); ++candidate)
                {
                    const std::size_t previous = remaining[(candidate + remaining.size() - 1u) % remaining.size()];
                    const std::size_t current = remaining[candidate];
                    const std::size_t next = remaining[(candidate + 1u) % remaining.size()];
                    if (orientation * cross2(points[previous], points[current], points[next]) <= epsilon) continue;
                    bool containsPoint = false;
                    for (const std::size_t other : remaining)
                    {
                        if (other == previous || other == current || other == next) continue;
                        if (inside(points[other], points[previous], points[current], points[next]))
                        { containsPoint = true; break; }
                    }
                    if (containsPoint) continue;
                    triangles.push_back({ corners[previous], corners[current], corners[next] });
                    remaining.erase(remaining.begin() + static_cast<std::ptrdiff_t>(candidate));
                    clipped = true;
                    break;
                }
                if (!clipped) Fail(line, corners.front().Column, "polygon is self-intersecting or cannot be triangulated.");
            }
            triangles.push_back({ corners[remaining[0]], corners[remaining[1]], corners[remaining[2]] });
            return triangles;
        }
    }

    /// Input: complete OBJ source bytes. Output: validated indexed Kairo mesh.
    /// Materials, lines, and points fail explicitly because mesh artifact v1
    /// cannot preserve those semantics. Object/group names are non-semantic here.
    [[nodiscard]] inline MeshArtifactData ParseOBJMesh(std::span<const std::byte> source)
    {
        using namespace obj_import_detail;
        if (source.size() > MaximumDerivedArtifactPayloadBytes)
            Fail(1u, 1u, "source exceeds the 1 GiB safety limit.");
        const std::string_view text(reinterpret_cast<const char*>(source.data()), source.size());
        if (text.find('\0') != std::string_view::npos) Fail(1u, 1u, "source contains a NUL byte.");
        std::vector<std::array<float, 3u>> positions;
        std::vector<std::array<float, 2u>> texCoords;
        std::vector<std::array<float, 3u>> normals;
        MeshArtifactData mesh;
        mesh.HasNormals = true;
        std::vector<bool> generatedNormal;
        std::vector<std::array<double, 3u>> accumulatedNormals;
        std::vector<std::pair<std::size_t, std::size_t>> normalLocations;
        std::unordered_map<VertexKey, std::uint32_t, VertexKeyHash> vertices;
        std::unordered_map<std::string, std::uint64_t> smoothingGroups;
        std::uint64_t nextSmoothingGroup = 2u;
        std::uint64_t smoothingGroup = 0u;
        std::uint64_t faceSerial = 0u;
        std::optional<bool> usesTexCoords;

        std::size_t lineNumber = 0u;
        std::size_t lineStart = 0u;
        while (lineStart <= text.size())
        {
            ++lineNumber;
            std::size_t lineEnd = text.find('\n', lineStart);
            if (lineEnd == std::string_view::npos) lineEnd = text.size();
            std::string_view line = text.substr(lineStart, lineEnd - lineStart);
            if (!line.empty() && line.back() == '\r') line.remove_suffix(1u);
            const std::vector<Token> tokens = Tokenize(line);
            if (!tokens.empty())
            {
                const Token& command = tokens.front();
                if (command.Text == "v")
                {
                    if (tokens.size() < 4u || tokens.size() > 5u)
                        Fail(lineNumber, command.Column, "vertex requires x y z and optional w.");
                    double x = ParseNumber(tokens[1], lineNumber, "vertex x");
                    double y = ParseNumber(tokens[2], lineNumber, "vertex y");
                    double z = ParseNumber(tokens[3], lineNumber, "vertex z");
                    if (tokens.size() == 5u)
                    {
                        const double w = ParseNumber(tokens[4], lineNumber, "vertex w");
                        if (std::abs(w) <= 1.0e-30) Fail(lineNumber, tokens[4].Column, "vertex w cannot be zero.");
                        x /= w; y /= w; z /= w;
                    }
                    if (positions.size() == std::numeric_limits<std::uint32_t>::max())
                        Fail(lineNumber, command.Column, "position count exceeds 32-bit addressing.");
                    positions.push_back({ static_cast<float>(x), static_cast<float>(y), static_cast<float>(z) });
                    if (!mesh_artifact_detail::IsFinite(positions.back()))
                        Fail(lineNumber, command.Column, "vertex exceeds finite 32-bit range.");
                }
                else if (command.Text == "vt")
                {
                    if (tokens.size() < 2u || tokens.size() > 4u)
                        Fail(lineNumber, command.Column, "texcoord requires u and optional v/w.");
                    const double u = ParseNumber(tokens[1], lineNumber, "texcoord u");
                    const double v = tokens.size() >= 3u ? ParseNumber(tokens[2], lineNumber, "texcoord v") : 0.0;
                    if (tokens.size() == 4u) (void)ParseNumber(tokens[3], lineNumber, "texcoord w");
                    if (texCoords.size() == std::numeric_limits<std::int32_t>::max())
                        Fail(lineNumber, command.Column, "texcoord count exceeds 31-bit addressing.");
                    texCoords.push_back({ static_cast<float>(u), static_cast<float>(v) });
                    if (!mesh_artifact_detail::IsFinite(texCoords.back()))
                        Fail(lineNumber, command.Column, "texcoord exceeds finite 32-bit range.");
                }
                else if (command.Text == "vn")
                {
                    if (tokens.size() != 4u) Fail(lineNumber, command.Column, "normal requires x y z.");
                    const std::array<double, 3u> value{ ParseNumber(tokens[1], lineNumber, "normal x"),
                        ParseNumber(tokens[2], lineNumber, "normal y"), ParseNumber(tokens[3], lineNumber, "normal z") };
                    if (normals.size() == std::numeric_limits<std::int32_t>::max())
                        Fail(lineNumber, command.Column, "normal count exceeds 31-bit addressing.");
                    normals.push_back(Normalize(value, lineNumber, command.Column, "normal"));
                }
                else if (command.Text == "s")
                {
                    if (tokens.size() != 2u) Fail(lineNumber, command.Column, "smoothing requires one group or off.");
                    if (tokens[1].Text == "off" || tokens[1].Text == "0") smoothingGroup = 0u;
                    else if (tokens[1].Text == "on" || tokens[1].Text == "1") smoothingGroup = 1u;
                    else
                    {
                        const std::string name(tokens[1].Text);
                        const auto [entry, inserted] = smoothingGroups.emplace(name, nextSmoothingGroup);
                        if (inserted) ++nextSmoothingGroup;
                        smoothingGroup = entry->second;
                    }
                }
                else if (command.Text == "f")
                {
                    if (tokens.size() < 4u) Fail(lineNumber, command.Column, "face requires at least three vertices.");
                    if (positions.empty()) Fail(lineNumber, command.Column, "face appears before any positions.");
                    std::vector<Corner> corners;
                    corners.reserve(tokens.size() - 1u);
                    for (std::size_t index = 1u; index < tokens.size(); ++index)
                    {
                        Corner corner = ParseCorner(tokens[index], lineNumber, positions.size(), texCoords.size(), normals.size());
                        const bool hasTexCoord = corner.TexCoord >= 0;
                        if (usesTexCoords && *usesTexCoords != hasTexCoord)
                            Fail(lineNumber, tokens[index].Column, "all face vertices must consistently provide texcoords.");
                        usesTexCoords = hasTexCoord;
                        corners.push_back(corner);
                    }
                    ++faceSerial;
                    for (const TriangleCorners& triangle : Triangulate(corners, positions, lineNumber))
                    {
                        if (mesh.Indices.size() > std::numeric_limits<std::uint32_t>::max() - 3u)
                            Fail(lineNumber, command.Column, "index count exceeds 32-bit addressing.");
                        try
                        {
                            (void)mesh_artifact_detail::PayloadSize(mesh.Vertices.size(), mesh.Indices.size() + 3u,
                                true, usesTexCoords.value_or(false));
                        }
                        catch (const std::length_error&)
                        {
                            Fail(lineNumber, command.Column, "derived mesh exceeds the 1 GiB safety limit.");
                        }
                        const std::array<Corner, 3u> triangleCorners{ triangle.A, triangle.B, triangle.C };
                        const auto ab = Subtract(positions[triangle.B.Position], positions[triangle.A.Position]);
                        const auto ac = Subtract(positions[triangle.C.Position], positions[triangle.A.Position]);
                        const auto faceNormal = Cross(ab, ac);
                        (void)Normalize(faceNormal, lineNumber, triangle.A.Column, "triangle");
                        for (const Corner& corner : triangleCorners)
                        {
                            const std::uint64_t smoothing = corner.Normal >= 0 ? 0u :
                                (smoothingGroup == 0u ? (std::uint64_t{ 1u } << 63u) | faceSerial : smoothingGroup);
                            const VertexKey key{ corner.Position, corner.TexCoord, corner.Normal, smoothing };
                            auto found = vertices.find(key);
                            std::uint32_t vertexIndex = 0u;
                            if (found == vertices.end())
                            {
                                if (mesh.Vertices.size() == std::numeric_limits<std::uint32_t>::max())
                                    Fail(lineNumber, corner.Column, "deduplicated vertex count exceeds 32-bit addressing.");
                                try
                                {
                                    (void)mesh_artifact_detail::PayloadSize(mesh.Vertices.size() + 1u,
                                        mesh.Indices.size() + 3u, true, usesTexCoords.value_or(false));
                                }
                                catch (const std::length_error&)
                                {
                                    Fail(lineNumber, corner.Column, "derived mesh exceeds the 1 GiB safety limit.");
                                }
                                MeshArtifactVertex vertex;
                                vertex.Position = positions[corner.Position];
                                if (corner.TexCoord >= 0) vertex.TexCoord = texCoords[static_cast<std::size_t>(corner.TexCoord)];
                                const bool generated = corner.Normal < 0;
                                if (!generated) vertex.Normal = normals[static_cast<std::size_t>(corner.Normal)];
                                vertexIndex = static_cast<std::uint32_t>(mesh.Vertices.size());
                                mesh.Vertices.push_back(vertex);
                                generatedNormal.push_back(generated);
                                accumulatedNormals.push_back({});
                                normalLocations.emplace_back(lineNumber, corner.Column);
                                vertices.emplace(key, vertexIndex);
                            }
                            else vertexIndex = found->second;
                            if (generatedNormal[vertexIndex])
                                for (std::size_t axis = 0u; axis < 3u; ++axis)
                                    accumulatedNormals[vertexIndex][axis] += faceNormal[axis];
                            mesh.Indices.push_back(vertexIndex);
                        }
                    }
                }
                else if (command.Text == "o" || command.Text == "g")
                {
                    // Names do not affect geometry and mesh artifact v1 has one logical mesh.
                }
                else if (command.Text == "usemtl" || command.Text == "mtllib")
                    Fail(lineNumber, command.Column, "material partitions require a future mesh artifact version.");
                else if (command.Text == "l" || command.Text == "p" || command.Text == "vp")
                    Fail(lineNumber, command.Column, "line, point, and parameter-space geometry is unsupported.");
                else
                    Fail(lineNumber, command.Column, "unknown or unsupported command '" + std::string(command.Text) + "'.");
            }
            if (lineEnd == text.size()) break;
            lineStart = lineEnd + 1u;
        }
        if (mesh.Indices.empty()) Fail(1u, 1u, "source contains no triangle faces.");
        mesh.HasTexCoords = usesTexCoords.value_or(false);
        for (std::size_t index = 0u; index < mesh.Vertices.size(); ++index)
            if (generatedNormal[index])
                mesh.Vertices[index].Normal = Normalize(accumulatedNormals[index],
                    normalLocations[index].first, normalLocations[index].second, "generated normal");
        ValidateMeshArtifactData(mesh);
        return mesh;
    }

    /// Working OBJ plugin for geometry-only Wavefront sources. Runtime scale,
    /// translation, and material binding remain scene concerns, so V1 accepts
    /// no importer settings and produces deterministic object-local geometry.
    class OBJMeshImporter final : public AssetImporter
    {
    public:
        [[nodiscard]] std::string Identifier() const override { return "kairo.obj"; }
        [[nodiscard]] std::string Version() const override { return "1"; }
        [[nodiscard]] DerivedArtifact Import(const ImportRequest& request) const override
        {
            if (request.ExpectedType != AssetType::Mesh)
                throw std::invalid_argument("OBJ importer requires a mesh asset target.");
            if (!request.Record.CanonicalSettings.empty())
                throw std::invalid_argument("OBJ importer version 1 accepts no settings.");
            return MakeMeshDerivedArtifact(ParseOBJMesh(request.SourceBytes));
        }
    };
}
