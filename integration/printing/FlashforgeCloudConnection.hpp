#pragma once

#include "../FlashNetwork.h"
#include <chrono>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace Slic3r::VendorPrinting {

struct CloudConnectionImpl;

// Construct this on the account worker from a fresh authenticated session.
// No token getter: the UI receives connection state and device events only.
class CloudConnectionSession {
public:
    CloudConnectionSession(std::string client_id, std::string access_token,
                           std::chrono::steady_clock::time_point expires_at,
                           std::uint64_t account_epoch,
                           bool authenticated = true);
private:
    friend struct CloudConnectionImpl;
    friend class FlashforgeCloudConnection;
    std::string m_client_id;
    std::string m_access_token;
    std::chrono::steady_clock::time_point m_expires_at;
    std::uint64_t m_account_epoch;
    bool m_authenticated;
};

// Called both by the account worker and SDK callback threads. The owner must
// supply a synchronized snapshot; never call a sequential account object here.
using CloudConnectionSessionProvider = std::function<std::optional<CloudConnectionSession>()>;

struct FlashforgeConnectionApi {
    decltype(&fnet_getMqttConfig) mqtt_config = nullptr;
    decltype(&fnet_freeMqttConfig) free_config = nullptr;
    decltype(&fnet_createConnection) create = nullptr;
    decltype(&fnet_connectionStop) stop = nullptr;
    decltype(&fnet_freeConnection) free_connection = nullptr;
    decltype(&fnet_connectionSubscribe) subscribe = nullptr;
    decltype(&fnet_connectionSend) send = nullptr;
    decltype(&fnet_connectionSendMulti) send_multi = nullptr;
    decltype(&fnet_allocString) allocate_string = nullptr;
    decltype(&fnet_freeString) free_string = nullptr;
    decltype(&fnet_freeSyncLoginInfo) free_login = nullptr;
    decltype(&fnet_freeSyncBindInfo) free_bind = nullptr;
    decltype(&fnet_freeSyncOnlineInfo) free_online = nullptr;
    decltype(&fnet_freeDevDetail) free_detail = nullptr;
    bool complete() const;
};

enum class CloudConnectionState { unsupported, closed, connecting, disconnected, ready, authentication_required, fault };

struct CloudConnectionEvent {
    fnet_conn_read_data_type_t type = FNET_CONN_READ_DEVICE_KEEP_ALIVE;
    std::string device_id;
    std::string status;
    std::string firmware_version;
    std::string payload; // Notifications only; parse vendor fields outside callbacks.
};

// Public operations belong to one sequential worker. Callback threads only
// update synchronized state/queues. The initialized-library owner outlives all
// connection callbacks and stop/free; this adapter never initializes the SDK.
class FlashforgeCloudConnection {
public:
    FlashforgeCloudConnection(FlashforgeConnectionApi api,
                             std::shared_ptr<void> initialized_library_owner,
                             CloudConnectionSessionProvider session_provider);
    ~FlashforgeCloudConnection();
    FlashforgeCloudConnection(const FlashforgeCloudConnection&) = delete;
    FlashforgeCloudConnection& operator=(const FlashforgeCloudConnection&) = delete;

    bool open(const std::vector<std::string>& device_topics,
              std::chrono::milliseconds connect_timeout = std::chrono::milliseconds(10000));
    // After a reconnect, explicitly restore subscriptions before a new operator
    // action. This never replays a previous print/start command.
    bool prepare();
    // One dispatch, never replayed on reconnect. Success means SDK acceptance,
    // not a printer acknowledgement. Only subscribed device topics are allowed.
    int control_job(const std::string& topic, const std::string& job_id, const std::string& action);
    void close();
    CloudConnectionState state() const;
    std::string message() const;
    std::vector<CloudConnectionEvent> drain_events();

    // Assign this to FlashforgeCloudApi::send and use workflow_context() as
    // CloudSessionView::connection. The result buffer still belongs to the SDK
    // and must be freed through the original fnet_freeWriteMultiResult pointer.
    static int send_checked(void* context, const fnet_conn_write_multi_data_t* data,
                            fnet_conn_write_multi_result_t** result);
    void* workflow_context() { return this; }

private:
    std::unique_ptr<CloudConnectionImpl> m_impl;
};

} // namespace Slic3r::VendorPrinting
