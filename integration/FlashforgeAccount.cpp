#include "FlashforgeAccount.hpp"
#include "FlashNetwork.h"
#include "printing/FlashforgeCloudConnection.hpp"
#include "printing/FlashforgeCloudPrint.hpp"
#include <chrono>
#include <cstring>
#include <filesystem>
#include <iomanip>
#include <ios>
#include <memory>
#include <ostream>
#include <random>
#include <sstream>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>
#ifdef _WIN32
#include <windows.h>
#else
#include <dlfcn.h>
#endif

namespace Slic3r {
namespace {
constexpr int timeout_ms = 15000;
std::string text(const char* value) { return value ? value : ""; }
std::string new_client_id()
{
    std::random_device random;
    std::ostringstream result;
    result << "pc_" << std::hex << std::setfill('0');
    for (int i = 0; i < 4; ++i) result << std::setw(8) << static_cast<unsigned int>(random());
    return result.str();
}
}

struct FlashforgeAccount::Library {
#ifdef _WIN32
    HMODULE handle = nullptr;
#else
    void* handle = nullptr;
#endif
    decltype(&fnet_uninitlize) uninitialize = nullptr;
    bool initialized = false;
    explicit Library(const std::string& path)
    {
        if (!std::filesystem::path(path).is_absolute()) return;
#ifdef _WIN32
        const auto native = std::filesystem::u8path(path).wstring();
        handle = LoadLibraryExW(native.c_str(), nullptr,
            LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_DEFAULT_DIRS);
#else
        handle = dlopen(path.c_str(), RTLD_NOW | RTLD_LOCAL);
#endif
    }
    ~Library()
    {
        if (initialized && uninitialize) uninitialize();
        if (!handle) return;
#ifdef _WIN32
        FreeLibrary(handle);
#else
        dlclose(handle);
#endif
    }
    template<class T> T symbol(const char* name) const
    {
        if (!handle) return nullptr;
#ifdef _WIN32
        return reinterpret_cast<T>(GetProcAddress(handle, name));
#else
        return reinterpret_cast<T>(dlsym(handle, name));
#endif
    }
};

bool FlashforgeApi::complete() const
{
    return login && refresh && free_token && free_string && logout && devices && free_devices;
}

FlashforgeAccount::FlashforgeAccount(FlashforgeApi api, VendorPrinting::FlashforgeCloudApi cloud_api,
    VendorPrinting::FlashforgeConnectionApi connection_api, std::shared_ptr<void> library_owner)
    : m_library(std::move(library_owner)), m_api(api), m_cloud_api(cloud_api),
      m_connection_api(connection_api), m_client_id(new_client_id())
{
    if (m_api.complete()) {
        m_state = State::signed_out;
        m_message = "Flashforge is ready to sign in.";
    } else {
        m_message = "Flashforge networking plugin is unavailable.";
    }
}
FlashforgeAccount::~FlashforgeAccount() = default;

std::string FlashforgeAccount::plugin_version(const std::string& path)
{
    Library library(path);
    auto version = library.symbol<decltype(&fnet_getVersion)>("fnet_getVersion");
    return version ? text(version()) : "unavailable";
}

std::shared_ptr<FlashforgeAccount> FlashforgeAccount::load(const std::string& path,
                                                         const std::string& settings)
{
    auto account = std::make_shared<FlashforgeAccount>(FlashforgeApi{});
    auto library = std::make_shared<Library>(path);
    if (!library->handle) return account;
    auto version = library->symbol<decltype(&fnet_getVersion)>("fnet_getVersion");
    if (!version || text(version()) != "3.3.6") {
        account->m_message = "Flashforge plugin ABI is incompatible; this adapter requires version 3.3.6.";
        return account;
    }
    auto initialize = library->symbol<decltype(&fnet_initlize)>("fnet_initlize");
    library->uninitialize = library->symbol<decltype(&fnet_uninitlize)>("fnet_uninitlize");
    auto user_agent = library->symbol<decltype(&fnet_setUserAgent)>("fnet_setUserAgent");
    FlashforgeApi api;
    api.login = library->symbol<decltype(api.login)>("fnet_getTokenByPassword");
    api.refresh = library->symbol<decltype(api.refresh)>("fnet_refreshToken");
    api.free_token = library->symbol<decltype(api.free_token)>("fnet_freeToken");
    api.free_string = library->symbol<decltype(api.free_string)>("fnet_freeString");
    api.logout = library->symbol<decltype(api.logout)>("fnet_signOut");
    api.devices = library->symbol<decltype(api.devices)>("fnet_getWanDevList");
    api.free_devices = library->symbol<decltype(api.free_devices)>("fnet_freeWanDevList");
    api.detail = library->symbol<decltype(api.detail)>("fnet_getWanDevProductDetail");
    api.free_product = library->symbol<decltype(api.free_product)>("fnet_freeDevProduct");
    api.free_detail = library->symbol<decltype(api.free_detail)>("fnet_freeDevDetail");
    auto& cloud = account->m_cloud_api;
    cloud.upload = library->symbol<decltype(cloud.upload)>("fnet_wanDevSendGcodeClound");
    cloud.free_upload = library->symbol<decltype(cloud.free_upload)>("fnet_freeCloundGcodeData");
    cloud.create_job = library->symbol<decltype(cloud.create_job)>("fnet_wanDevAddCloundJob");
    cloud.free_jobs = library->symbol<decltype(cloud.free_jobs)>("fnet_freeAddCloudJobResults");
    cloud.send = library->symbol<decltype(cloud.send)>("fnet_connectionSendMulti");
    cloud.free_send = library->symbol<decltype(cloud.free_send)>("fnet_freeWriteMultiResult");
    auto& connection = account->m_connection_api;
    connection.mqtt_config = library->symbol<decltype(connection.mqtt_config)>("fnet_getMqttConfig");
    connection.free_config = library->symbol<decltype(connection.free_config)>("fnet_freeMqttConfig");
    connection.create = library->symbol<decltype(connection.create)>("fnet_createConnection");
    connection.stop = library->symbol<decltype(connection.stop)>("fnet_connectionStop");
    connection.free_connection = library->symbol<decltype(connection.free_connection)>("fnet_freeConnection");
    connection.subscribe = library->symbol<decltype(connection.subscribe)>("fnet_connectionSubscribe");
    connection.send = library->symbol<decltype(connection.send)>("fnet_connectionSend");
    connection.send_multi = cloud.send;
    connection.allocate_string = library->symbol<decltype(connection.allocate_string)>("fnet_allocString");
    connection.free_string = api.free_string;
    connection.free_login = library->symbol<decltype(connection.free_login)>("fnet_freeSyncLoginInfo");
    connection.free_bind = library->symbol<decltype(connection.free_bind)>("fnet_freeSyncBindInfo");
    connection.free_online = library->symbol<decltype(connection.free_online)>("fnet_freeSyncOnlineInfo");
    connection.free_detail = api.free_detail;
    if (!api.complete() || !initialize || !library->uninitialize || !user_agent) {
        account->m_message = "Flashforge plugin is missing required account functions.";
        return account;
    }
    if (!std::filesystem::is_regular_file(std::filesystem::u8path(settings)) ||
        std::filesystem::u8path(settings).filename() != DAT_FILE_NAME) {
        account->m_message = "Select the FLASHNETWORK9.DAT settings supplied with the compatible plugin.";
        return account;
    }
    // Do not enable vendor HTTP logging: it may contain account credentials.
    fnet_log_settings_t logs{};
    logs.level = FNET_LOG_LEVEL_OFF;
    logs.fileDir = "";
    if (initialize(settings.c_str(), &logs) != FNET_OK) {
        account->m_message = "Flashforge plugin could not initialize with these server settings.";
        return account;
    }
    library->initialized = true;
    // Identify this application honestly instead of impersonating a vendor app.
    user_agent("OrqaSlicer/0.1.0 (account-integration-preview)");
    account->m_api = api;
    account->m_library = std::move(library);
    account->m_state = State::signed_out;
    account->m_message = "Flashforge is ready to sign in.";
    return account;
}

void FlashforgeAccount::clear_session()
{
    if (++m_account_epoch == 0) ++m_account_epoch;
    m_live_status.reset();
    m_access_token.clear();
    m_refresh_token.clear();
    m_devices.clear();
    if (m_state != State::unavailable) m_state = State::signed_out;
}

bool FlashforgeAccount::accept_token(int result, fnet_token_data_t* token, char* message)
{
    // Never show a raw server message, which may echo sensitive fields.
    const std::unique_ptr<char, decltype(m_api.free_string)> message_guard(message, m_api.free_string);
    const std::unique_ptr<fnet_token_data_t, decltype(m_api.free_token)> token_guard(token, m_api.free_token);
    const bool valid = result == FNET_OK && token && token->accessToken &&
        *token->accessToken && token->refreshToken && *token->refreshToken && token->expiresIn > 0;
    if (valid) {
        m_access_token = token->accessToken;
        m_refresh_token = token->refreshToken;
        m_expiry = std::chrono::steady_clock::now() + std::chrono::seconds(token->expiresIn);
        m_state = State::signed_in;
        m_message = "Signed in to Flashforge.";
    } else {
        if (result == FNET_UNAUTHORIZED || result == FNET_INVALID_VALIDATION) {
            clear_session();
            m_message = "Flashforge sign-in expired or was rejected. Please sign in again.";
        } else {
            m_message = "Flashforge could not connect. Check the network and try again.";
        }
    }
    return valid;
}

bool FlashforgeAccount::login(const std::string& username, const std::string& password)
{
    if (m_state == State::unavailable) return false;
    if (m_state == State::signed_in) {
        m_message = "Sign out of Flashforge before switching its account.";
        return false;
    }
    if (username.empty() || password.empty()) {
        m_message = "Enter your Flashforge email and password.";
        return false;
    }
    fnet_token_data_t* token = nullptr;
    char* message = nullptr;
    int result = m_api.login(username.c_str(), password.c_str(), "en", &token, &message, timeout_ms);
    if (!accept_token(result, token, message)) return false;
    return refresh_devices();
}

bool FlashforgeAccount::restore(const std::string& refresh_token)
{
    if (m_state == State::unavailable || refresh_token.empty()) return false;
    m_refresh_token = refresh_token; // Retain remembered sign-in on a transient network failure.
    fnet_token_data_t* token = nullptr;
    char* message = nullptr;
    int result = m_api.refresh(refresh_token.c_str(), "en", &token, &message, timeout_ms);
    if (!accept_token(result, token, message)) return false;
    return fetch_devices(false);
}

bool FlashforgeAccount::refresh_devices()
{
    return fetch_devices(true);
}

bool FlashforgeAccount::fetch_devices(bool retry_refresh)
{
    if (m_state != State::signed_in) return false;
    if (std::chrono::steady_clock::now() >= m_expiry) {
        // Copy: accept_token replaces the stored refresh token.
        const std::string secret = m_refresh_token;
        return restore(secret);
    }
    fnet_wan_dev_info_t* infos = nullptr;
    int count = 0;
#ifdef _WIN32
    constexpr long long platform = 14;
#else
    constexpr long long platform = 15;
#endif
    const int result = m_api.devices(m_client_id.c_str(), m_access_token.c_str(), 31, platform,
                                    &infos, &count, timeout_ms);
    auto release_devices = [this, count](fnet_wan_dev_info_t* value) {
        if (value) m_api.free_devices(value, count < 0 ? 0 : count);
    };
    const std::unique_ptr<fnet_wan_dev_info_t, decltype(release_devices)> devices_guard(infos, release_devices);
    if (result != FNET_OK || count < 0 || count > 10000 || (count && !infos)) {
        m_devices.clear(); // No stale devices presented as a current result.
        if (result == FNET_UNAUTHORIZED) {
            if (retry_refresh && !m_refresh_token.empty()) {
                const std::string secret = m_refresh_token;
                return restore(secret);
            }
            clear_session();
            m_message = "Flashforge session expired. Please sign in again.";
        } else {
            m_message = "Signed in, but the Flashforge printer list could not be refreshed. Try again.";
        }
        return false;
    }
    m_devices.clear();
    for (int i = 0; i < count; ++i) {
        if (!infos[i].devId || !*infos[i].devId) continue;
        m_devices.push_back({"flashforge:" + text(infos[i].devId), text(infos[i].name),
                             text(infos[i].model), text(infos[i].status),
                             text(infos[i].serialNumber), text(infos[i].devTopic),
                             text(infos[i].updateInfo.status) == "device",
                             text(infos[i].updateInfo.title).substr(0, 1024), text(infos[i].updateInfo.tips).substr(0, 1024)});
    }
    m_message = "Signed in to Flashforge. Printer list refreshed.";
    return true;
}

void FlashforgeAccount::logout()
{
    const bool available = m_state != State::unavailable;
    const std::string access = m_access_token;
    clear_session(); // Local logout succeeds even if cloud is unreachable.
    if (available) {
        m_message = "Signed out of Flashforge on this computer.";
        if (!access.empty() && m_api.logout(access.c_str(), timeout_ms) != FNET_OK)
            m_message += " Cloud sign-out could not be confirmed.";
    }
}

} // namespace Slic3r
