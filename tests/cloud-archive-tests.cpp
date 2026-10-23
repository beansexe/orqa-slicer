#include "printing/FlashforgeArchiveInspector.hpp"
#include "printing/FlashforgeCloudPrint.hpp"
#include <filesystem>
#include <fstream>
#include <iostream>
#include <initializer_list>
#include <ios>
#include <stdexcept>
#include <string>
int main(int argc, char** argv) {
    if (argc != 2) return 2;
    const auto root = std::filesystem::absolute(argv[1]);
    Slic3r::VendorPrinting::CloudPrintRequest request;
    request.printer.model_id = "Flashforge-Adventurer-5M";
    request.printer.nozzle_diameter = .4;
    request.plate_number = 0;
    request.thumbnail_path = (root / "thumbnail.png").u8string();
    // Test the inspector's parser/integrity gates, not the existing MD5 library.
    const Slic3r::VendorPrinting::CloudMd5Reader hash = [](const std::string&, std::string& digest) { digest.assign(32, 'a'); return true; };
    const auto inspect = Slic3r::VendorPrinting::make_flashforge_archive_inspector(hash);
    request.job_path = (root / "valid.3mf").u8string();
    if (!inspect(request)) throw std::runtime_error("valid sliced plate rejected");
    using Slic3r::VendorPrinting::CloudArchiveError;
    using Slic3r::VendorPrinting::validate_flashforge_archive;
    request.printer.model_id = "Flashforge Adventurer 5M";
    if (validate_flashforge_archive(request, hash).error != CloudArchiveError::model_mismatch)
        throw std::runtime_error("display name must not substitute for the native catalog ID");
    request.printer.model_id = "Flashforge-Adventurer-5M";
    for (const auto* name : {"missing-gcode", "model-only", "wrong-model", "wrong-nozzle", "wrong-plate", "duplicate-entry", "bad-xml", "dtd", "duplicate-metadata", "no-motion", "bad-reference", "oversized-xml"}) {
        request.job_path = (root / (std::string(name) + ".3mf")).u8string();
        if (inspect(request)) throw std::runtime_error(std::string("invalid archive accepted: ") + name);
    }
    request.job_path = (root / "wrong-nozzle.3mf").u8string();
    if (validate_flashforge_archive(request, hash).error != CloudArchiveError::nozzle_mismatch) throw std::runtime_error("nozzle diagnostic");
    request.job_path = (root / "wrong-plate.3mf").u8string();
    if (validate_flashforge_archive(request, hash).error != CloudArchiveError::missing_plate) throw std::runtime_error("plate diagnostic");
    request.job_path = (root / "missing-gcode.3mf").u8string();
    if (validate_flashforge_archive(request, hash).error != CloudArchiveError::missing_gcode) throw std::runtime_error("gcode diagnostic");
    request.job_path = (root / "valid.3mf").u8string();
    request.thumbnail_path = (root / "bad.png").u8string();
    if (inspect(request)) throw std::runtime_error("invalid PNG accepted");
    if (validate_flashforge_archive(request, hash).error != CloudArchiveError::invalid_thumbnail) throw std::runtime_error("thumbnail diagnostic");
    request.thumbnail_path = (root / "thumbnail.png").u8string();
    request.job_path = (root / "mutated.3mf").u8string();
    bool mutate = true;
    const auto changing = Slic3r::VendorPrinting::make_flashforge_archive_inspector([&mutate](const std::string& file, std::string& hash) {
        hash.assign(32, 'a');
        if (mutate) { mutate = false; std::ofstream stream(std::filesystem::u8path(file), std::ios::binary | std::ios::app); stream.put('x'); }
        return true;
    });
    if (changing(request)) throw std::runtime_error("archive mutation accepted");
    request.job_path = (root / "valid.3mf").u8string();
    if (validate_flashforge_archive(request, [](const std::string&, std::string&) { return false; }).error != CloudArchiveError::checksum_failed) throw std::runtime_error("checksum diagnostic");
    request.plate_number = -1;
    if (validate_flashforge_archive(request, hash).error != CloudArchiveError::invalid_request) throw std::runtime_error("request diagnostic");
    std::cout << "Archive validation passed native catalog-ID regression, valid plate, 14 malformed/mutation cases and stage diagnostics.\n";
}
