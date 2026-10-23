#pragma once

#include "FlashforgeCloudPrint.hpp"
#include <functional>
#include <string>

namespace Slic3r::VendorPrinting {

// Root supplies a wrapper around libslic3r::bbl_calc_md5. It must read the actual
// current file bytes and return false on I/O errors; no cached metadata hashes.
using CloudMd5Reader = std::function<bool(const std::string&, std::string&)>;

// Supported: pinned Orca/Orca-Flashforge sliced 3MF with model_settings.config,
// slice_info.config and Metadata/plate_<one-based>.gcode; one verified nozzle.
// Uses existing miniz, Expat and libpng. No upload, printer or account calls.
CloudFileInspector make_flashforge_archive_inspector(CloudMd5Reader md5_reader);

} // namespace Slic3r::VendorPrinting
