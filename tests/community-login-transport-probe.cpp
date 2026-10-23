#include <windows.h>
#include <filesystem>
#include <iostream>
#include <string>

// An intentionally invalid, synthetic credential checks transport only. Never
// read an installed account or print the response body, which may contain data.
int wmain(int argc, wchar_t** argv)
{
    if (argc != 3) return 2;
    const auto dll = LoadLibraryExW(argv[1], nullptr,
        LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_DEFAULT_DIRS);
    if (!dll) return 3;
    auto create = reinterpret_cast<void*(*)(std::string)>(GetProcAddress(dll, "bambu_network_create_agent"));
    auto destroy = reinterpret_cast<int(*)(void*)>(GetProcAddress(dll, "bambu_network_destroy_agent"));
    auto profile = reinterpret_cast<int(*)(void*, std::string, unsigned int*, std::string*)>(GetProcAddress(dll, "bambu_network_get_my_profile"));
    if (!create || !destroy || !profile) return 4;
    std::filesystem::create_directories(argv[2]);
    void* agent = create(std::filesystem::path(argv[2]).u8string());
    if (!agent) return 5;
    unsigned int status = 0;
    std::string body;
    const int result = profile(agent, "orqa-synthetic-invalid-transport-probe", &status, &body);
    destroy(agent);
    FreeLibrary(dll);
    std::cout << "Synthetic profile request: HTTP " << status << ", result " << result << '\n';
    // 401/403 are expected; zero means failure before an HTTP response.
    return status == 0 ? 1 : 0;
}
