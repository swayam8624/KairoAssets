module;

#include <filesystem>
#include <map>
#include <stdexcept>
#include <utility>
#include <vector>

export module Kairo.Assets.SourceWatcher;

import Kairo.Assets.AssetID;
import Kairo.Assets.ImportDatabase;

export namespace kairo::assets
{
    /// A state transition emitted by the portable source observation layer.
    /// `Current` means a provenance update made an asset valid again; callers
    /// normally schedule reimport only for Changed and Missing transitions.
    struct SourceChange final
    {
        AssetID Asset;
        SourceImportState Previous = SourceImportState::Current;
        SourceImportState Current = SourceImportState::Current;
        friend constexpr bool operator==(const SourceChange&, const SourceChange&) noexcept = default;
    };

    /// Deterministic polling observer for project source assets. This is the
    /// portability baseline behind editor reimport indicators and headless
    /// build tools. Native FSEvents/inotify/ReadDirectoryChangesW adapters may
    /// request Poll() earlier, but they must not change these event semantics.
    class SourceWatcher final
    {
    public:
        explicit SourceWatcher(std::filesystem::path projectRoot) : m_ProjectRoot(std::move(projectRoot))
        {
            if (m_ProjectRoot.empty()) throw std::invalid_argument("Source watcher requires a project root.");
        }

        /// Establishes a baseline without producing artificial startup events.
        void Synchronize(const ImportDatabase& imports)
        {
            StateMap next;
            for (const ImportRecord& record : imports.Snapshot())
                next.emplace(record.Asset, imports.Evaluate(m_ProjectRoot, record.Asset));
            m_States.swap(next);
        }

        /// Input: current validated provenance. Output: deterministic asset-ID
        /// ordered transitions since the last poll. New assets are baselined;
        /// callers do not receive a false "changed" event merely for discovery.
        [[nodiscard]] std::vector<SourceChange> Poll(const ImportDatabase& imports)
        {
            StateMap next;
            std::vector<SourceChange> changes;
            for (const ImportRecord& record : imports.Snapshot())
            {
                const SourceImportState current = imports.Evaluate(m_ProjectRoot, record.Asset);
                const auto previous = m_States.find(record.Asset);
                if (previous != m_States.end() && previous->second != current)
                    changes.push_back({ record.Asset, previous->second, current });
                next.emplace(record.Asset, current);
            }
            m_States.swap(next);
            return changes;
        }

    private:
        using StateMap = std::map<AssetID, SourceImportState>;
        std::filesystem::path m_ProjectRoot;
        StateMap m_States;
    };
}
