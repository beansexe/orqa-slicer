#pragma once

#include <string>

namespace Slic3r {
// Choice belongs to this executable revision. The running choice is frozen on
// first use, so saving a new choice cannot mix networking/camera DLLs mid-session.
bool community_bambu_requested();
bool community_bambu_running();
bool community_bambu_available();
bool set_community_bambu_requested(bool enabled);
std::string community_bambu_library(const std::string& filename);
} // namespace Slic3r
