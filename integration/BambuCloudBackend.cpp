#include "BambuCloudBackend.hpp"

#include <wx/file.h>
#include <wx/filename.h>
#include <wx/stdpaths.h>
#include <wx/string.h>
#include <filesystem>
#include <fstream>
#include <ios>
#include <string>
#include <system_error>

namespace Slic3r {
namespace {
std::filesystem::path revision_directory()
{
    return std::filesystem::u8path(wxStandardPaths::Get().GetExecutablePath().ToUTF8().data()).parent_path();
}
}

bool community_bambu_requested()
{
    std::ifstream input(revision_directory() / "bambu-backend.txt", std::ios::binary);
    char text[32]{};
    input.read(text, sizeof(text));
    return input.gcount() == 16 && std::string(text, 16) == "community-cloud\n";
}

bool community_bambu_running()
{
    static const bool running = community_bambu_requested();
    return running;
}

bool community_bambu_available()
{
    std::error_code error;
    const auto directory = revision_directory() / "community-bambu";
    return std::filesystem::is_regular_file(directory / "bambu_networking.dll", error) &&
        std::filesystem::is_regular_file(directory / "BambuSource.dll", error);
}

bool set_community_bambu_requested(bool enabled)
{
    // Freeze before writing even if no networking module was loaded at startup.
    (void)community_bambu_running();
    if (enabled && !community_bambu_available()) return false;
    const auto path = (revision_directory() / "bambu-backend.txt").u8string();
    wxTempFile output(wxString::FromUTF8(path));
    return output.IsOpened() && output.Write(enabled ? "community-cloud\n" : "standard\n") && output.Commit();
}

std::string community_bambu_library(const std::string& filename)
{
    if (!community_bambu_running()) return {};
    return (revision_directory() / "community-bambu" / filename).u8string();
}
} // namespace Slic3r
