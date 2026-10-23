#pragma once

// FlashNetwork.h is the AGPL interface published by FlashForge/Orca-Flashforge.
// No vendor implementation or networking binary is included here.
#include "FlashNetwork.h"
#include "printing/FlashforgeCloudConnection.hpp"
#include "printing/FlashforgeCloudPrint.hpp"
#include <atomic>
#include <chrono>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace Slic3r {

struct FlashforgeDevice {
    std::string id; // Provider-qualified, never a model name.
    std::string name;
    std::string model;
    std::string status;
    std::string serial_number;
    std::string topic;
};

struct FlashforgeLiveStatus {
    std::string id;
    std::string status;
    std::string firmware;
    std::string print_file;
    std::optional<double> nozzle_temperature, nozzle_target, bed_temperature, bed_target, progress;
    int layer = 0;
    int total_layers = 0;
};

// Narrow ABI boundary, injectable to test session failures without cloud calls.
struct FlashforgeApi {
    decltype(&fnet_getTokenByPassword) login = nullptr;
    decltype(&fnet_refreshToken) refresh = nullptr;
    decltype(&fnet_freeToken) free_token = nullptr;
    decltype(&fnet_freeString) free_string = nullptr;
    decltype(&fnet_signOut) logout = nullptr;
    decltype(&fnet_getWanDevList) devices = nullptr;
    decltype(&fnet_freeWanDevList) free_devices = nullptr;
    decltype(&fnet_getWanDevProductDetail) detail = nullptr;
    decltype(&fnet_freeDevProduct) free_product = nullptr;
    decltype(&fnet_freeDevDetail) free_detail = nullptr;
    bool complete() const;
};

// All operations are sequential on the account worker. Never use this object
// concurrently. Bambu's NetworkAgent and session store are not referenced.
class FlashforgeAccount {
public:
    enum class State { unavailable, signed_out, signed_in };
    explicit FlashforgeAccount(FlashforgeApi api,
        VendorPrinting::FlashforgeCloudApi cloud_api = {},
        VendorPrinting::FlashforgeConnectionApi connection_api = {},
        std::shared_ptr<void> library_owner = {}); // Injectable test boundary.
    ~FlashforgeAccount();
    FlashforgeAccount(const FlashforgeAccount&) = delete;
    FlashforgeAccount& operator=(const FlashforgeAccount&) = delete;

    static std::shared_ptr<FlashforgeAccount> load(const std::string& library,
                                                 const std::string& settings);
    static std::string plugin_version(const std::string& library);
    bool login(const std::string& username, const std::string& password);
    bool restore(const std::string& refresh_token);
    bool refresh_devices();
    bool refresh_status(const std::string& qualified_id);
    const std::optional<FlashforgeLiveStatus>& live_status() const { return m_live_status; }
    VendorPrinting::CloudPrintResult send_cloud(VendorPrinting::CloudPrintRequest request,
        VendorPrinting::CloudFileInspector inspector, const std::atomic<bool>& cancelled);
    void logout();
    State state() const { return m_state; }
    const std::string& message() const { return m_message; }
    const std::vector<FlashforgeDevice>& devices() const { return m_devices; }
    // Only the OS credential store may receive this value. Never display/log it.
    const std::string& session_secret() const { return m_refresh_token; }

private:
    struct Library;
    std::shared_ptr<void> m_library;
    FlashforgeApi m_api;
    VendorPrinting::FlashforgeCloudApi m_cloud_api;
    VendorPrinting::FlashforgeConnectionApi m_connection_api;
    std::optional<FlashforgeLiveStatus> m_live_status;
    std::uint64_t m_account_epoch = 1;
    State m_state = State::unavailable;
    std::string m_message;
    std::string m_access_token;
    std::string m_refresh_token;
    std::string m_client_id;
    std::vector<FlashforgeDevice> m_devices;
    std::chrono::steady_clock::time_point m_expiry;
    bool accept_token(int result, fnet_token_data_t* token, char* message);
    bool fetch_devices(bool retry_refresh);
    bool fetch_status(const std::string& qualified_id, bool retry_refresh);
    void clear_session();
};

} // namespace Slic3r
