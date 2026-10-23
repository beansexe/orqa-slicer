#pragma once

#include "FlashforgeCloudPrint.hpp"
#include <functional>
#include <optional>
#include <string>

namespace Slic3r::VendorPrinting {

// Root supplies a wrapper around libslic3r::bbl_calc_md5. It must read the actual
// current file bytes and return false on I/O errors; no cached metadata hashes.
using CloudMd5Reader = std::function<bool(const std::string&, std::string&)>;

enum class CloudArchiveError {
    none, invalid_request, unreadable_files, checksum_failed, invalid_archive,
    missing_metadata, invalid_metadata, missing_plate, model_mismatch,
    nozzle_mismatch, missing_gcode, invalid_gcode, invalid_thumbnail,
    changed_files, read_failed
};
struct CloudArchiveValidation {
    std::optional<ValidatedCloudFiles> files;
    CloudArchiveError error = CloudArchiveError::none;
};

// Diagnostic values contain no file contents, paths, credentials or raw errors.
CloudArchiveValidation validate_flashforge_archive(const CloudPrintRequest& request,
                                                  const CloudMd5Reader& md5_reader);

// Supported: pinned Orca/Orca-Flashforge sliced 3MF with model_settings.config,
// slice_info.config and Metadata/plate_<one-based>.gcode; one verified nozzle.
// Uses existing miniz, Expat and libpng. No upload, printer or account calls.
CloudFileInspector make_flashforge_archive_inspector(CloudMd5Reader md5_reader);

} // namespace Slic3r::VendorPrinting
