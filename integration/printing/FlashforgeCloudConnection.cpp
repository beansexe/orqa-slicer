#include "FlashforgeCloudConnection.hpp"
#include "../FlashNetwork.h"
#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <limits>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace Slic3r::VendorPrinting {
namespace {
constexpr int auth_timeout_ms = 15000;
constexpr std::size_t max_events = 256;

bool valid_text(const std::string& value)
{
    if (value.empty()) return false;
    for (unsigned char c : value) if (c < 32 || c == 127) return false;
    return true;
}

std::string text(const char* value) { return value ? value : ""; }
}

CloudConnectionSession::CloudConnectionSession(std::string client_id, std::string access_token,
                                             std::chrono::steady_clock::time_point expires_at,
                                             std::uint64_t account_epoch,
                                             bool authenticated)
    : m_client_id(std::move(client_id)), m_access_token(std::move(access_token)),
      m_expires_at(expires_at), m_account_epoch(account_epoch), m_authenticated(authenticated)
{}

bool FlashforgeConnectionApi::complete() const
{
    return mqtt_config && free_config && create && stop && free_connection && subscribe && send &&
        send_multi && allocate_string && free_string && free_login && free_bind && free_online && free_detail;
}

struct CloudConnectionImpl {
    const FlashforgeConnectionApi api;
    const std::shared_ptr<void> library_owner;
    const CloudConnectionSessionProvider session_provider;
    void* connection = nullptr; // Sequential worker only.
    mutable std::mutex mutex;
    std::mutex auth_mutex;
    std::condition_variable changed;
    std::atomic<bool> connected{false};
    std::atomic<bool> ready{false};
    std::atomic<bool> closing{true};
    std::atomic<bool> authentication_failed{false};
    std::atomic<bool> callback_failed{false};
    std::atomic<bool> authentication_active{false};
    std::atomic<std::uint64_t> generation{0};
    std::size_t callbacks = 0;
    std::string client_id;
    std::uint64_t account_epoch = 0;
    std::string user_topic;
    std::vector<std::string> common_topics;
    std::vector<std::string> device_topics;
    std::vector<CloudConnectionEvent> events;
    std::string status_message = "Flashforge cloud connection is closed.";
    bool connecting = false;

    CloudConnectionImpl(FlashforgeConnectionApi api_value, std::shared_ptr<void> owner,
                        CloudConnectionSessionProvider provider)
        : api(api_value), library_owner(std::move(owner)), session_provider(std::move(provider))
    {}

    struct CallbackGuard {
        CloudConnectionImpl& owner;
        explicit CallbackGuard(CloudConnectionImpl& value) : owner(value)
        {
            std::lock_guard<std::mutex> lock(owner.mutex);
            ++owner.callbacks;
        }
        ~CallbackGuard()
        {
            std::lock_guard<std::mutex> lock(owner.mutex);
            --owner.callbacks;
            owner.changed.notify_all();
        }
    };

    struct AuthenticationActivity {
        CloudConnectionImpl& owner;
        explicit AuthenticationActivity(CloudConnectionImpl& value) : owner(value)
        {
            owner.authentication_active.store(true);
            owner.ready.store(false);
            owner.generation.fetch_add(1);
        }
        ~AuthenticationActivity()
        {
            owner.authentication_active.store(false);
            owner.changed.notify_all();
        }
    };

    void message(const char* value)
    {
        std::lock_guard<std::mutex> lock(mutex);
        status_message = value;
    }

    void fail_callback() noexcept
    {
        callback_failed.store(true);
        ready.store(false);
        changed.notify_all();
    }

    std::optional<CloudConnectionSession> session()
    {
        auto value = session_provider ? session_provider() : std::nullopt;
        if (!value || !value->m_authenticated || value->m_account_epoch == 0 || !valid_text(value->m_client_id) ||
            value->m_access_token.empty() || value->m_expires_at <= std::chrono::steady_clock::now()) {
            authentication_failed.store(true);
            ready.store(false);
            changed.notify_all();
            return std::nullopt;
        }
        return value;
    }

    bool same_client(const CloudConnectionSession& value)
    {
        std::lock_guard<std::mutex> lock(mutex);
        if (client_id == value.m_client_id && account_epoch == value.m_account_epoch) return true;
        authentication_failed.store(true);
        ready.store(false);
        return false;
    }

    bool fetch_config(const CloudConnectionSession& value)
    {
        fnet_mqtt_config_t* raw = nullptr;
        const int code = api.mqtt_config(value.m_client_id.c_str(), value.m_access_token.c_str(), &raw, auth_timeout_ms);
        std::unique_ptr<fnet_mqtt_config_t, decltype(api.free_config)> config(raw, api.free_config);
        if (value.m_expires_at <= std::chrono::steady_clock::now()) authentication_failed.store(true);
        if (code == FNET_UNAUTHORIZED) authentication_failed.store(true);
        if (authentication_failed.load() || code != FNET_OK || !config || !valid_text(text(config->userTopic)) || config->commonTopicCnt < 0 ||
            config->commonTopicCnt > 100 || (config->commonTopicCnt > 0 && !config->commonTopics)) return false;
        std::vector<std::string> topics;
        for (int i = 0; i < config->commonTopicCnt; ++i) {
            auto topic = text(config->commonTopics[i]);
            if (!valid_text(topic)) return false;
            topics.push_back(std::move(topic));
        }
        std::lock_guard<std::mutex> lock(mutex);
        user_topic = config->userTopic;
        common_topics = std::move(topics);
        return true;
    }

    bool subscriptions()
    {
        if (!connected.load() || closing.load() || authentication_failed.load() || callback_failed.load() || authentication_active.load()) return false;
        auto fresh = session();
        if (!fresh || !same_client(*fresh)) return false;
        const auto before = generation.load();
        std::vector<std::string> topics;
        std::string user;
        {
            std::lock_guard<std::mutex> lock(mutex);
            user = user_topic;
            topics.push_back(user);
            topics.insert(topics.end(), common_topics.begin(), common_topics.end());
            topics.insert(topics.end(), device_topics.begin(), device_topics.end());
        }
        std::sort(topics.begin(), topics.end());
        topics.erase(std::unique(topics.begin(), topics.end()), topics.end());
        for (std::size_t offset = 0; offset < topics.size(); offset += 100) {
            std::vector<const char*> topic_pointers;
            const auto end = std::min(topics.size(), offset + 100);
            for (auto i = offset; i < end; ++i) topic_pointers.push_back(topics[i].c_str());
            fnet_conn_subscribe_data_t data{topic_pointers.data(), static_cast<int>(topic_pointers.size())};
            if (api.subscribe(connection, &data) != FNET_OK) return false;
        }
        fnet_conn_write_data_t sync{FNET_CONN_WRITE_SYNC_LOGIN, fresh->m_client_id.c_str(), user.c_str(), 1};
        if (api.send(connection, &sync) != FNET_OK) return false;
        {
            std::lock_guard<std::mutex> lock(mutex);
            if (!connected.load() || closing.load() || authentication_failed.load() || callback_failed.load() ||
                authentication_active.load() || generation.load() != before) return false;
            ready.store(true);
        }
        return true;
    }

    static void status_callback(fnet_conn_status_t status, void* context) noexcept
    {
        auto& self = *static_cast<CloudConnectionImpl*>(context);
        try {
            CallbackGuard callback(self);
            bool owner_matches = false;
            if (!self.closing.load()) {
                auto fresh = self.session();
                owner_matches = fresh && self.same_client(*fresh);
            }
            std::lock_guard<std::mutex> lock(self.mutex);
            self.ready.store(false);
            self.generation.fetch_add(1);
            self.connected.store(status == FNET_CONN_STATUS_CONNECTED && !self.closing.load() && owner_matches);
            self.changed.notify_all();
        } catch (...) { self.fail_callback(); }
    }

    static int update_callback(const char** new_client_id, void* context) noexcept
    {
        auto& self = *static_cast<CloudConnectionImpl*>(context);
        if (new_client_id) *new_client_id = nullptr;
        try {
            CallbackGuard callback(self);
            std::lock_guard<std::mutex> auth_lock(self.auth_mutex);
            AuthenticationActivity activity(self);
            if (!new_client_id || self.closing.load()) return 1;
            auto fresh = self.session();
            if (!fresh || !self.same_client(*fresh) || !self.fetch_config(*fresh) || self.closing.load() ||
                fresh->m_client_id.size() > static_cast<std::size_t>(std::numeric_limits<int>::max())) return 1;
            *new_client_id = self.api.allocate_string(fresh->m_client_id.c_str(), static_cast<int>(fresh->m_client_id.size()));
            return *new_client_id ? 0 : 1; // The SDK takes ownership on success.
        } catch (...) { self.fail_callback(); return 1; }
    }

    void free_read(fnet_conn_read_data_t* data) noexcept
    {
        if (!data) return;
        if (data->data) {
            switch (data->type) {
            case FNET_CONN_READ_SYS_NOTIFY:
            case FNET_CONN_READ_UPDATE_NOTIFY:
            case FNET_CONN_READ_DEVICE_KEEP_ALIVE: api.free_string(static_cast<char*>(data->data)); break;
            case FNET_CONN_READ_SYNC_LOGIN: api.free_login(static_cast<fnet_sync_login_info_t*>(data->data)); break;
            case FNET_CONN_READ_SYNC_BIND_DEVICE:
            case FNET_CONN_READ_SYNC_UNBIND_DEVICE: api.free_bind(static_cast<fnet_sync_bind_info_t*>(data->data)); break;
            case FNET_CONN_READ_SYNC_ONLINE:
            case FNET_CONN_READ_SYNC_OFFLINE: api.free_online(static_cast<fnet_sync_online_info_t*>(data->data)); break;
            case FNET_CONN_READ_DEVICE_DETAIL: api.free_detail(static_cast<fnet_dev_detail_t*>(data->data)); break;
            case FNET_CONN_READ_SYNC_USER_PROFILE:
            case FNET_CONN_READ_SYNC_UNREGISTER_USER: break; // SDK contract: no payload.
            default: fail_callback(); break; // Unknown payload has no safe generic deallocator.
            }
        }
        if (data->topic) api.free_string(data->topic);
    }

    static void read_callback(fnet_conn_read_data_t* data, void* context) noexcept
    {
        auto& self = *static_cast<CloudConnectionImpl*>(context);
        try {
            CallbackGuard callback(self);
            const auto cleanup = [&self](fnet_conn_read_data_t* value) { self.free_read(value); };
            std::unique_ptr<fnet_conn_read_data_t, decltype(cleanup)> read(data, cleanup);
            if (!data || self.closing.load()) return;
            auto fresh = self.session();
            if (!fresh || !self.same_client(*fresh)) return;
            CloudConnectionEvent event;
            event.type = data->type;
            switch (data->type) {
            case FNET_CONN_READ_SYS_NOTIFY:
            case FNET_CONN_READ_UPDATE_NOTIFY: event.payload = text(static_cast<char*>(data->data)); break;
            case FNET_CONN_READ_DEVICE_KEEP_ALIVE: event.device_id = text(static_cast<char*>(data->data)); break;
            case FNET_CONN_READ_SYNC_UNREGISTER_USER:
                self.authentication_failed.store(true); self.ready.store(false); break;
            case FNET_CONN_READ_SYNC_LOGIN:
                if (const auto* login = static_cast<fnet_sync_login_info_t*>(data->data)) {
                    std::lock_guard<std::mutex> lock(self.mutex);
                    if (text(login->clientType) == "pc" && text(login->clientId) != self.client_id) {
                        self.authentication_failed.store(true); self.ready.store(false);
                    }
                }
                break;
            case FNET_CONN_READ_SYNC_BIND_DEVICE:
            case FNET_CONN_READ_SYNC_UNBIND_DEVICE:
                if (const auto* bind = static_cast<fnet_sync_bind_info_t*>(data->data)) event.device_id = text(bind->devId);
                break;
            case FNET_CONN_READ_SYNC_ONLINE:
            case FNET_CONN_READ_SYNC_OFFLINE:
                if (const auto* online = static_cast<fnet_sync_online_info_t*>(data->data)) event.device_id = text(online->devId);
                event.status = data->type == FNET_CONN_READ_SYNC_ONLINE ? "online" : "offline";
                break;
            case FNET_CONN_READ_DEVICE_DETAIL:
                if (const auto* detail = static_cast<fnet_dev_detail_t*>(data->data)) {
                    event.device_id = text(detail->devId);
                    event.status = text(detail->status);
                    event.firmware_version = text(detail->firmwareVersion);
                }
                break;
            case FNET_CONN_READ_SYNC_USER_PROFILE: break;
            default: self.fail_callback(); return;
            }
            std::lock_guard<std::mutex> lock(self.mutex);
            if (self.events.size() == max_events) self.events.erase(self.events.begin());
            self.events.push_back(std::move(event));
        } catch (...) { self.fail_callback(); }
    }

    void close()
    {
        closing.store(true);
        ready.store(false);
        connected.store(false);
        generation.fetch_add(1);
        if (connection) {
            api.stop(connection);
            {
                std::unique_lock<std::mutex> lock(mutex);
                changed.wait(lock, [this]() { return callbacks == 0; });
            }
            // Vendor freeConnection must quiesce/join its SDK-owned threads.
            // The pinned vendor also calls stop before freeConnection.
            api.free_connection(connection);
            connection = nullptr;
        }
        std::lock_guard<std::mutex> lock(mutex);
        connecting = false;
        client_id.clear();
        account_epoch = 0;
        user_topic.clear();
        common_topics.clear();
        device_topics.clear();
        events.clear();
        status_message = "Flashforge cloud connection is closed.";
    }

    int publish(const fnet_conn_write_multi_data_t* data, fnet_conn_write_multi_result_t** output)
    {
        if (output) *output = nullptr;
        if (!output || !data || !connection || !ready.load() || !connected.load() || closing.load() ||
            callback_failed.load() || authentication_failed.load() || authentication_active.load()) return FNET_CONN_SEND_ERROR;
        auto fresh = session();
        if (!fresh || !same_client(*fresh)) return FNET_UNAUTHORIZED;
        if (data->type != FNET_CONN_WRITE_START_CLOUND_JOB || data->topicCnt != 1 || data->dataCnt != 1 ||
            !data->topics || !data->datas || !data->topics[0] || !data->datas[0] || data->qos != 1) return FNET_ERROR;
        std::uint64_t before;
        {
            std::lock_guard<std::mutex> lock(mutex);
            if (std::find(device_topics.begin(), device_topics.end(), data->topics[0]) == device_topics.end()) return FNET_ERROR;
            if (!ready.load() || !connected.load() || closing.load() || callback_failed.load() ||
                authentication_failed.load() || authentication_active.load()) return FNET_CONN_SEND_ERROR;
            before = generation.load();
        }
        // SDK ABI has no publish timeout or printer acknowledgement callback.
        const int code = api.send_multi(connection, data, output);
        auto after = session();
        if (!after || !same_client(*after)) return FNET_CONN_SEND_ERROR;
        if (generation.load() != before || !ready.load() || !connected.load() || closing.load() ||
            authentication_failed.load() || callback_failed.load() || fresh->m_expires_at <= std::chrono::steady_clock::now()) return FNET_CONN_SEND_ERROR;
        return code;
    }
};

FlashforgeCloudConnection::FlashforgeCloudConnection(FlashforgeConnectionApi api, std::shared_ptr<void> owner,
                                                   CloudConnectionSessionProvider provider)
    : m_impl(std::make_unique<CloudConnectionImpl>(api, std::move(owner), std::move(provider)))
{}

FlashforgeCloudConnection::~FlashforgeCloudConnection() { m_impl->close(); }

bool FlashforgeCloudConnection::open(const std::vector<std::string>& device_topics, std::chrono::milliseconds timeout)
{
    auto& self = *m_impl;
    self.close();
    if (!self.api.complete() || !self.library_owner || !self.session_provider) {
        self.message("This Flashforge plugin does not provide the required cloud connection functions.");
        return false;
    }
    if (timeout.count() <= 0 || timeout > std::chrono::milliseconds(60000) || device_topics.empty()) {
        self.message("Select a Flashforge device and a valid connection timeout.");
        return false;
    }
    for (const auto& topic : device_topics) {
        if (!valid_text(topic) || topic.find_first_of("+#") != std::string::npos) {
            self.message("The selected Flashforge device topic is invalid.");
            return false;
        }
    }
    self.authentication_failed.store(false);
    self.callback_failed.store(false);
    try {
        auto fresh = self.session();
        if (!fresh) { self.message("Sign in to Flashforge again before connecting."); return false; }
        {
            std::lock_guard<std::mutex> lock(self.mutex);
            self.client_id = fresh->m_client_id;
            self.account_epoch = fresh->m_account_epoch;
            self.device_topics = device_topics;
        }
        if (!self.fetch_config(*fresh)) { self.message("Flashforge cloud connection settings could not be obtained."); return false; }
        self.closing.store(false);
        {
            std::lock_guard<std::mutex> lock(self.mutex);
            self.connecting = true;
        }
        fnet_conn_settings_t settings{self.client_id.c_str(), CloudConnectionImpl::status_callback, &self,
            CloudConnectionImpl::update_callback, &self, CloudConnectionImpl::read_callback, &self};
        const int code = self.api.create(&self.connection, &settings);
        if (code != FNET_OK || !self.connection) {
            self.close(); self.message("Flashforge cloud connection could not be created."); return false;
        }
        {
            std::unique_lock<std::mutex> lock(self.mutex);
            const bool observed = self.changed.wait_for(lock, timeout, [&self]() {
                return self.connected.load() || self.authentication_failed.load() || self.callback_failed.load();
            });
            self.connecting = false;
            if (!observed || !self.connected.load()) {
                lock.unlock(); self.close(); self.message("Flashforge connection readiness was not confirmed before the timeout."); return false;
            }
        }
        if (!self.subscriptions()) { self.close(); self.message("Flashforge cloud subscriptions could not be confirmed."); return false; }
        self.message("Flashforge cloud transport is ready. Printer execution still requires status confirmation.");
        return true;
    } catch (...) {
        self.callback_failed.store(true);
        self.close(); self.message("Flashforge cloud connection preparation failed."); return false;
    }
}

bool FlashforgeCloudConnection::prepare()
{
    try {
        if (!m_impl->connection || !m_impl->subscriptions()) {
            m_impl->message("Flashforge cloud transport needs reconnection or sign-in before sending."); return false;
        }
        m_impl->message("Flashforge cloud transport is ready. Printer execution still requires status confirmation.");
        return true;
    } catch (...) { m_impl->fail_callback(); return false; }
}

void FlashforgeCloudConnection::close() { m_impl->close(); }

CloudConnectionState FlashforgeCloudConnection::state() const
{
    const auto& self = *m_impl;
    if (!self.api.complete() || !self.library_owner || !self.session_provider) return CloudConnectionState::unsupported;
    if (self.authentication_failed.load()) return CloudConnectionState::authentication_required;
    if (self.callback_failed.load()) return CloudConnectionState::fault;
    if (self.closing.load()) return CloudConnectionState::closed;
    if (self.ready.load() && self.connected.load()) return CloudConnectionState::ready;
    std::lock_guard<std::mutex> lock(self.mutex);
    return self.connecting ? CloudConnectionState::connecting : CloudConnectionState::disconnected;
}

std::string FlashforgeCloudConnection::message() const
{
    std::lock_guard<std::mutex> lock(m_impl->mutex);
    return m_impl->status_message;
}

std::vector<CloudConnectionEvent> FlashforgeCloudConnection::drain_events()
{
    std::lock_guard<std::mutex> lock(m_impl->mutex);
    std::vector<CloudConnectionEvent> result;
    result.swap(m_impl->events);
    return result;
}

int FlashforgeCloudConnection::send_checked(void* context, const fnet_conn_write_multi_data_t* data,
                                         fnet_conn_write_multi_result_t** result)
{
    if (!context) { if (result) *result = nullptr; return FNET_ERROR; }
    auto& self = *static_cast<FlashforgeCloudConnection*>(context);
    try { return self.m_impl->publish(data, result); }
    catch (...) { self.m_impl->fail_callback(); return FNET_CONN_SEND_ERROR; }
}

int FlashforgeCloudConnection::control_job(const std::string& topic, const std::string& job_id, const std::string& action)
{
    auto& self = *m_impl;
    try {
        if (!valid_text(job_id) || job_id.size() > 256 ||
            (action != "pause" && action != "continue" && action != "cancel")) return FNET_ERROR;
        auto fresh = self.session();
        if (!fresh || !self.same_client(*fresh)) return FNET_UNAUTHORIZED;
        std::uint64_t before;
        {
            std::lock_guard<std::mutex> lock(self.mutex);
            if (std::find(self.device_topics.begin(), self.device_topics.end(), topic) == self.device_topics.end()) return FNET_ERROR;
            if (!self.connection || !self.ready.load() || !self.connected.load() || self.closing.load() ||
                self.callback_failed.load() || self.authentication_failed.load() || self.authentication_active.load()) return FNET_CONN_SEND_ERROR;
            before = self.generation.load();
        }
        const fnet_job_ctrl_t job{job_id.c_str(), action.c_str()};
        const fnet_conn_write_data_t data{FNET_CONN_WRITE_JOB_CTRL, &job, topic.c_str(), 1};
        const int code = self.api.send(self.connection, &data);
        auto after = self.session();
        if (!after || !self.same_client(*after) || self.generation.load() != before || !self.ready.load() ||
            !self.connected.load() || self.callback_failed.load() || self.authentication_failed.load() ||
            self.authentication_active.load() || fresh->m_expires_at <= std::chrono::steady_clock::now()) return FNET_CONN_SEND_ERROR;
        return code;
    } catch (...) { self.fail_callback(); return FNET_CONN_SEND_ERROR; }
}

} // namespace Slic3r::VendorPrinting
