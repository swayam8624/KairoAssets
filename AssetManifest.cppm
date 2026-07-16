module;

#include <algorithm>
#include <charconv>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <limits>
#include <optional>
#include <ranges>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

export module Kairo.Assets.Manifest;

import Kairo.Assets.AssetID;
import Kairo.Assets.AtomicFile;
import Kairo.Assets.Metadata;
import Kairo.Assets.Registry;
import Kairo.Assets.Types;

export namespace kairo::assets
{
    class AssetManifestError final : public std::runtime_error
    {
    public:
        AssetManifestError(std::size_t line, std::size_t column, std::string message)
            : std::runtime_error("Asset manifest " + std::to_string(line) + ":" +
                std::to_string(column) + ": " + message), Line(line), Column(column) {}

        std::size_t Line;
        std::size_t Column;
    };

    namespace manifest_detail
    {
        constexpr std::size_t MaxManifestBytes = 64u * 1024u * 1024u;
        constexpr std::size_t MaxAssets = 1'000'000u;
        constexpr std::size_t MaxDependenciesPerAsset = 65'535u;
        constexpr std::size_t MaxPathBytes = 4096u;
        constexpr std::size_t MaxImporterBytes = 128u;

        struct Token final
        {
            std::string Text;
            std::size_t Column = 1u;
        };

        [[nodiscard]] inline std::vector<Token> TokenizeLine(std::string_view line, std::size_t lineNumber)
        {
            std::vector<Token> tokens;
            std::size_t index = 0u;
            while (index < line.size())
            {
                while (index < line.size() && (line[index] == ' ' || line[index] == '\t' || line[index] == '\r')) ++index;
                if (index == line.size() || line[index] == '#') break;

                Token token;
                token.Column = index + 1u;
                if (line[index] == '"')
                {
                    ++index;
                    bool closed = false;
                    while (index < line.size())
                    {
                        const char character = line[index++];
                        if (character == '"')
                        {
                            closed = true;
                            break;
                        }
                        if (character != '\\')
                        {
                            token.Text.push_back(character);
                            continue;
                        }
                        if (index == line.size()) throw AssetManifestError(lineNumber, index, "unfinished escape sequence");
                        const char escaped = line[index++];
                        switch (escaped)
                        {
                            case '\\': token.Text.push_back('\\'); break;
                            case '"': token.Text.push_back('"'); break;
                            case 'n': token.Text.push_back('\n'); break;
                            case 't': token.Text.push_back('\t'); break;
                            default: throw AssetManifestError(lineNumber, index, "unknown quoted-string escape");
                        }
                    }
                    if (!closed) throw AssetManifestError(lineNumber, token.Column, "unterminated quoted string");
                    if (index < line.size() && line[index] != ' ' && line[index] != '\t' && line[index] != '\r' && line[index] != '#')
                        throw AssetManifestError(lineNumber, index + 1u, "quoted token must be followed by whitespace");
                }
                else
                {
                    while (index < line.size() && line[index] != ' ' && line[index] != '\t' &&
                        line[index] != '\r' && line[index] != '#')
                        token.Text.push_back(line[index++]);
                }
                tokens.push_back(std::move(token));
            }
            return tokens;
        }

        inline void RequireCount(const std::vector<Token>& tokens, std::size_t expected,
            std::size_t line, std::string_view statement)
        {
            if (tokens.size() != expected)
            {
                const std::size_t column = tokens.size() > expected ? tokens[expected].Column : 1u;
                throw AssetManifestError(line, column, std::string(statement) + " expects " +
                    std::to_string(expected - 1u) + " argument(s)");
            }
        }

        [[nodiscard]] inline std::uint64_t ParseRevision(const Token& token, std::size_t line)
        {
            std::uint64_t result = 0u;
            const auto [end, error] = std::from_chars(
                token.Text.data(), token.Text.data() + token.Text.size(), result);
            if (error != std::errc{} || end != token.Text.data() + token.Text.size() || result == 0u)
                throw AssetManifestError(line, token.Column, "revision must be a positive unsigned integer");
            return result;
        }

        [[nodiscard]] inline std::string Quote(std::string_view value)
        {
            std::string result = "\"";
            for (const char character : value)
            {
                switch (character)
                {
                    case '\\': result += "\\\\"; break;
                    case '"': result += "\\\""; break;
                    case '\n': result += "\\n"; break;
                    case '\t': result += "\\t"; break;
                    default: result.push_back(character); break;
                }
            }
            result.push_back('"');
            return result;
        }
    }

    /// Input format:
    /// `kairo-assets 1`, followed by asset/dependency/end records.
    /// Output: fully validated metadata records in manifest order.
    /// Degeneracy: malformed syntax reports exact one-based line/column; graph
    /// errors are reported when records are installed into AssetRegistry.
    [[nodiscard]] inline std::vector<AssetMetadata> ParseAssetManifest(std::string_view source)
    {
        using namespace manifest_detail;
        if (source.size() > MaxManifestBytes) throw std::length_error("Asset manifest exceeds the 64 MiB safety limit.");

        std::vector<AssetMetadata> records;
        std::optional<AssetMetadata> current;
        bool headerSeen = false;
        std::istringstream input{ std::string(source) };
        std::string lineText;
        std::size_t lineNumber = 0u;
        while (std::getline(input, lineText))
        {
            ++lineNumber;
            const std::vector<Token> tokens = TokenizeLine(lineText, lineNumber);
            if (tokens.empty()) continue;

            if (!headerSeen)
            {
                RequireCount(tokens, 2u, lineNumber, "kairo-assets header");
                if (tokens[0].Text != "kairo-assets") throw AssetManifestError(lineNumber, tokens[0].Column, "expected kairo-assets header");
                if (tokens[1].Text != "1") throw AssetManifestError(lineNumber, tokens[1].Column, "unsupported manifest version");
                headerSeen = true;
                continue;
            }

            if (tokens[0].Text == "asset")
            {
                if (current.has_value()) throw AssetManifestError(lineNumber, tokens[0].Column, "nested asset record before end");
                RequireCount(tokens, 7u, lineNumber, "asset");
                if (records.size() >= MaxAssets) throw AssetManifestError(lineNumber, tokens[0].Column, "asset count exceeds safety limit");
                const auto type = ParseAssetType(tokens[2].Text);
                if (!type.has_value()) throw AssetManifestError(lineNumber, tokens[2].Column, "unknown asset type");
                const auto origin = ParseAssetOrigin(tokens[3].Text);
                if (!origin.has_value()) throw AssetManifestError(lineNumber, tokens[3].Column, "unknown asset origin");
                if (tokens[5].Text.size() > MaxPathBytes) throw AssetManifestError(lineNumber, tokens[5].Column, "asset path exceeds safety limit");
                if (tokens[6].Text.size() > MaxImporterBytes) throw AssetManifestError(lineNumber, tokens[6].Column, "importer identifier exceeds safety limit");
                try
                {
                    current = AssetMetadata{
                        AssetID::Parse(tokens[1].Text), *type, *origin, tokens[5].Text,
                        tokens[6].Text, ParseRevision(tokens[4], lineNumber), {}
                    };
                }
                catch (const AssetManifestError&)
                {
                    throw;
                }
                catch (const std::exception& error)
                {
                    throw AssetManifestError(lineNumber, tokens[1].Column, error.what());
                }
            }
            else if (tokens[0].Text == "dependency")
            {
                if (!current.has_value()) throw AssetManifestError(lineNumber, tokens[0].Column, "dependency outside asset record");
                RequireCount(tokens, 3u, lineNumber, "dependency");
                if (current->Dependencies.size() >= MaxDependenciesPerAsset)
                    throw AssetManifestError(lineNumber, tokens[0].Column, "dependency count exceeds safety limit");
                const auto type = ParseAssetType(tokens[2].Text);
                if (!type.has_value()) throw AssetManifestError(lineNumber, tokens[2].Column, "unknown dependency type");
                try
                {
                    current->Dependencies.push_back({ AssetID::Parse(tokens[1].Text), *type });
                }
                catch (const std::exception& error)
                {
                    throw AssetManifestError(lineNumber, tokens[1].Column, error.what());
                }
            }
            else if (tokens[0].Text == "end")
            {
                RequireCount(tokens, 1u, lineNumber, "end");
                if (!current.has_value()) throw AssetManifestError(lineNumber, tokens[0].Column, "end outside asset record");
                try
                {
                    ValidateAssetMetadata(*current);
                }
                catch (const std::exception& error)
                {
                    throw AssetManifestError(lineNumber, tokens[0].Column, error.what());
                }
                records.push_back(std::move(*current));
                current.reset();
            }
            else
            {
                throw AssetManifestError(lineNumber, tokens[0].Column, "unknown statement '" + tokens[0].Text + "'");
            }
        }

        if (!headerSeen) throw AssetManifestError(1u, 1u, "missing kairo-assets header");
        if (current.has_value()) throw AssetManifestError(lineNumber + 1u, 1u, "asset record is missing end");
        return records;
    }

    /// Output: deterministic, ID-sorted, diff-friendly manifest text.
    [[nodiscard]] inline std::string SerializeAssetManifest(const AssetRegistry& registry)
    {
        using namespace manifest_detail;
        std::ostringstream output;
        output << "kairo-assets 1\n";
        for (AssetMetadata metadata : registry.Snapshot())
        {
            std::ranges::sort(metadata.Dependencies, {}, &AssetReference::ID);
            output << "asset " << metadata.ID.ToString() << ' ' << NameOfAssetType(metadata.Type) << ' '
                << NameOfAssetOrigin(metadata.Origin) << ' ' << metadata.Revision << ' '
                << Quote(metadata.Path.generic_string()) << ' ' << Quote(metadata.Importer) << '\n';
            for (const AssetReference& dependency : metadata.Dependencies)
                output << "dependency " << dependency.ID.ToString() << ' '
                    << NameOfAssetType(dependency.Type) << '\n';
            output << "end\n";
        }
        return output.str();
    }

    inline void LoadAssetManifest(const std::filesystem::path& path, AssetRegistry& registry)
    {
        std::error_code error;
        const std::uintmax_t bytes = std::filesystem::file_size(path, error);
        if (error) throw std::runtime_error("Cannot inspect asset manifest: " + error.message());
        if (bytes > manifest_detail::MaxManifestBytes) throw std::length_error("Asset manifest exceeds the 64 MiB safety limit.");
        std::ifstream input(path, std::ios::binary);
        if (!input) throw std::runtime_error("Cannot open asset manifest for reading: " + path.string());
        std::string source(static_cast<std::size_t>(bytes), '\0');
        if (!source.empty() && !input.read(source.data(), static_cast<std::streamsize>(source.size())))
            throw std::runtime_error("Cannot read complete asset manifest: " + path.string());
        registry.ReplaceAll(ParseAssetManifest(source));
    }

    /// Task: save through a same-directory temporary file, flush it, then use
    /// the host's atomic rename/replace operation. A failed write or replace
    /// removes the temporary file and preserves the prior manifest.
    inline void SaveAssetManifest(const std::filesystem::path& path, const AssetRegistry& registry)
    {
        const std::filesystem::path parent = path.has_parent_path() ? path.parent_path() : std::filesystem::path(".");
        std::error_code error;
        std::filesystem::create_directories(parent, error);
        if (error) throw std::runtime_error("Cannot create asset manifest directory: " + error.message());
        const std::filesystem::path temporary = path.string() + ".tmp-" + GenerateAssetID().ToString();
        try
        {
            const std::string source = SerializeAssetManifest(registry);
            std::ofstream output(temporary, std::ios::binary | std::ios::trunc);
            if (!output) throw std::runtime_error("Cannot open temporary asset manifest for writing.");
            output.write(source.data(), static_cast<std::streamsize>(source.size()));
            output.flush();
            if (!output) throw std::runtime_error("Cannot write complete temporary asset manifest.");
            output.close();
            ReplaceFileAtomically(temporary, path);
        }
        catch (...)
        {
            std::filesystem::remove(temporary, error);
            throw;
        }
    }
}
