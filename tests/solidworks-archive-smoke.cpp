#include "printing/FlashforgeArchiveInspector.hpp"
#include "printing/FlashforgeCloudPrint.hpp"
#include <filesystem>
#include <iostream>
#include <stdexcept>
int main(int argc, char** argv) {
    if (argc != 2) return 2;
    const auto root = std::filesystem::absolute(argv[1]);
    Slic3r::VendorPrinting::CloudPrintRequest request;
    request.printer.model_id = "Flashforge-Adventurer-5M";
    request.printer.nozzle_diameter = .4;
    request.job_path = (root / "plate.3mf").u8string();
    request.thumbnail_path = (root / "thumbnail.png").u8string();
    const Slic3r::VendorPrinting::CloudMd5Reader hash = [](const std::string&, std::string& digest) { digest.assign(32, 'a'); return true; };
    auto result = Slic3r::VendorPrinting::validate_flashforge_archive(request, hash);
    if (!result.files) throw std::runtime_error("Real CLI archive rejected: " + std::to_string(static_cast<int>(result.error)));
    request.printer.nozzle_diameter = .6;
    if (Slic3r::VendorPrinting::validate_flashforge_archive(request, hash).files)
        throw std::runtime_error("Wrong nozzle accepted");
    std::cout << "Real background STEP archive passed production printer/nozzle/plate/G-code/PNG checks; wrong nozzle rejected. No upload.\n";
}
