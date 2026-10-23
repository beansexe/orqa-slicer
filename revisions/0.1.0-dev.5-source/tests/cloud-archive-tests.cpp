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
    request.printer.model_id = "Flashforge Adventurer 5M";
    request.printer.nozzle_diameter = .4;
    request.plate_number = 0;
    request.thumbnail_path = (root / "thumbnail.png").u8string();
    // Test the inspector's parser/integrity gates, not the existing MD5 library.
    const auto inspect = Slic3r::VendorPrinting::make_flashforge_archive_inspector([](const std::string&, std::string& hash) { hash.assign(32, 'a'); return true; });
    request.job_path = (root / "valid.3mf").u8string();
    if (!inspect(request)) throw std::runtime_error("valid sliced plate rejected");
    for (const auto* name : {"missing-gcode", "model-only", "wrong-model", "wrong-nozzle", "wrong-plate", "duplicate-entry", "bad-xml", "dtd", "duplicate-metadata", "no-motion", "bad-reference", "oversized-xml"}) {
        request.job_path = (root / (std::string(name) + ".3mf")).u8string();
        if (inspect(request)) throw std::runtime_error(std::string("invalid archive accepted: ") + name);
    }
    request.job_path = (root / "valid.3mf").u8string();
    request.thumbnail_path = (root / "bad.png").u8string();
    if (inspect(request)) throw std::runtime_error("invalid PNG accepted");
    request.thumbnail_path = (root / "thumbnail.png").u8string();
    request.job_path = (root / "mutated.3mf").u8string();
    bool mutate = true;
    const auto changing = Slic3r::VendorPrinting::make_flashforge_archive_inspector([&mutate](const std::string& file, std::string& hash) {
        hash.assign(32, 'a');
        if (mutate) { mutate = false; std::ofstream stream(std::filesystem::u8path(file), std::ios::binary | std::ios::app); stream.put('x'); }
        return true;
    });
    if (changing(request)) throw std::runtime_error("archive mutation accepted");
    std::cout << "Archive validation passed valid plate and 14 malformed/mutation cases.\n";
}
