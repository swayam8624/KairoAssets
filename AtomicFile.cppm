module;

#include <filesystem>
#include <stdexcept>
#include <string>
#include <system_error>

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

export module Kairo.Assets.AtomicFile;

export namespace kairo::assets
{
    /// Input: a fully written temporary file and a destination on the same volume.
    /// Output: the destination is replaced while preserving rename atomicity.
    /// Task: centralize the platform-specific final step used by manifests,
    /// scenes, and editor documents so every writer has the same failure policy.
    /// Preconditions: the temporary file exists and both paths are colocated.
    /// Degeneracy: failure throws and leaves the temporary file for the caller's
    /// cleanup guard; an existing destination is replaced on every supported host.
    inline void ReplaceFileAtomically(const std::filesystem::path& temporary,
        const std::filesystem::path& destination)
    {
#if defined(_WIN32)
        const DWORD flags = MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH;
        if (!MoveFileExW(temporary.c_str(), destination.c_str(), flags))
        {
            const std::error_code error(static_cast<int>(GetLastError()), std::system_category());
            throw std::runtime_error("Cannot atomically replace file: " + error.message());
        }
#else
        std::error_code error;
        std::filesystem::rename(temporary, destination, error);
        if (error)
            throw std::runtime_error("Cannot atomically replace file: " + error.message());
#endif
    }
}
