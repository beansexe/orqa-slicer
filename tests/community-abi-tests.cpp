#include "slic3r/Utils/bambu_networking.hpp"
// Both independent ABI headers use the upstream include guard name.
#undef __BAMBU_NETWORKING_HPP__
#define ABI_VERSION 0x020804
#include "../upstream/OpenBamboo/include/obn/bambu_networking.hpp"
#include <windows.h>
#include <cstddef>
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <string>

static_assert(sizeof(Slic3r::PrintParams) == sizeof(BBL::PrintParams), "Print parameter ABI size mismatch");
static_assert(offsetof(Slic3r::PrintParams, password) == offsetof(BBL::PrintParams, password), "Credential field ABI mismatch");
static_assert(offsetof(Slic3r::PrintParams, task_use_ams) == offsetof(BBL::PrintParams, task_use_ams), "AMS field ABI mismatch");
static_assert(offsetof(Slic3r::PrintParams, queue_plate_id) == offsetof(BBL::PrintParams, queue_plate_id), "Queue field ABI mismatch");

int wmain(int count, wchar_t** values)
{
    if (count != 4) return 2;
    auto network = LoadLibraryExW(values[1], nullptr, LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_DEFAULT_DIRS);
    auto camera = LoadLibraryExW(values[2], nullptr, LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_DEFAULT_DIRS);
    if (!network || !camera) throw std::runtime_error("A community library or runtime dependency could not load");
    for (const auto* name : {"bambu_network_get_version", "bambu_network_create_agent", "bambu_network_destroy_agent",
            "bambu_network_set_config_dir", "bambu_network_send_message", "bambu_network_start_print", "bambu_network_get_camera_url",
            "bambu_network_change_user", "bambu_network_connect_server", "bambu_network_get_user_print_info"})
        if (!GetProcAddress(network, name)) throw std::runtime_error("Required networking export missing");
    for (const auto* name : {"Bambu_Create", "Bambu_Open", "Bambu_StartStream", "Bambu_GetStreamInfo", "Bambu_ReadSample", "Bambu_Destroy"})
        if (!GetProcAddress(camera, name)) throw std::runtime_error("Required camera export missing");
    const auto directory = std::filesystem::path(values[3]);
    std::filesystem::create_directories(directory);
    auto create = reinterpret_cast<void*(*)(std::string)>(GetProcAddress(network, "bambu_network_create_agent"));
    auto configure = reinterpret_cast<int(*)(void*, std::string)>(GetProcAddress(network, "bambu_network_set_config_dir"));
    auto destroy = reinterpret_cast<int(*)(void*)>(GetProcAddress(network, "bambu_network_destroy_agent"));
    auto version = reinterpret_cast<std::string(*)()>(GetProcAddress(network, "bambu_network_get_version"));
    // No start/connect/login/print/camera-open function is called. These paths
    // initialize only an empty fixture profile and the local C++ object.
    void* agent = create(directory.u8string());
    if (!agent) throw std::runtime_error("Community agent could not be constructed");
    if (configure(agent, directory.u8string()) != 0 || version() != "02.08.04.99")
        throw std::runtime_error("Community agent version or configuration ABI mismatch");
    destroy(agent);
    FreeLibrary(camera);
    FreeLibrary(network);
    std::cout << "Print/AMS ABI layouts, required exports, relocated DLL dependencies and empty-profile agent lifecycle passed; no network functions called.\n";
}
