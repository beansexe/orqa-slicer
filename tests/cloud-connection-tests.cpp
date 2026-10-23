#include "printing/FlashforgeCloudConnection.hpp"
#include "FlashNetwork.h"
#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstring>
#include <exception>
#include <iostream>
#include <memory>
#include <mutex>
#include <optional>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

namespace {
using namespace Slic3r::VendorPrinting;
using namespace std::chrono_literals;

void require(bool condition, const char* message)
{
    if (!condition) throw std::runtime_error(message);
}

struct Mock {
    fnet_conn_settings_t settings{};
    std::mutex mutex;
    std::vector<std::string> calls;
    std::thread callback_thread;
    std::weak_ptr<int> library;
    int config_code = FNET_OK;
    int create_code = FNET_OK;
    int subscribe_code = FNET_OK;
    int sync_code = FNET_OK;
    int publish_code = FNET_OK;
    bool connect_on_create = true;
    bool delayed_connect = false;
    bool throw_config = false;
    bool fail_allocation = false;
    bool disconnect_on_publish = false;
    bool change_owner_on_publish = false;
    bool malformed_config = false;
    std::atomic<bool> expired{false};
    std::atomic<bool> changed_client{false};
    std::atomic<bool> changed_owner{false};
    std::atomic<bool> rotated_token{false};
    std::atomic<bool> throw_provider{false};
    std::atomic<bool> pause_provider{false};
    std::atomic<bool> provider_entered{false};
    std::atomic<int> configs_freed{0};
    std::atomic<int> strings_freed{0};
    std::atomic<int> login_freed{0};
    std::atomic<int> bind_freed{0};
    std::atomic<int> online_freed{0};
    std::atomic<int> detail_freed{0};
    std::atomic<int> publish_count{0};
    int job_controls = 0;
    std::atomic<int> free_count{0};

    ~Mock() { if (callback_thread.joinable()) callback_thread.join(); }
    void record(const char* value) { std::lock_guard<std::mutex> lock(mutex); calls.emplace_back(value); }
};
Mock* mock = nullptr;

char* copy(const char* value)
{
    const auto length = std::strlen(value);
    auto* result = new char[length + 1];
    std::memcpy(result, value, length + 1);
    return result;
}

int config(const char* client, const char* secret, fnet_mqtt_config_t** output, int timeout)
{
    mock->record("config");
    require(std::string(client) == "client-id" && (std::string(secret) == "private-token" || std::string(secret) == "rotated-token") && timeout > 0, "wrong config session");
    if (mock->throw_config) throw std::runtime_error("simulated SDK failure");
    *output = new fnet_mqtt_config_t{copy(mock->malformed_config ? "" : "user-topic"), new char*[1]{copy("common-topic")}, 1};
    return mock->config_code;
}

void free_config(fnet_mqtt_config_t* value)
{
    ++mock->configs_freed;
    delete[] value->userTopic;
    for (int i = 0; i < value->commonTopicCnt; ++i) delete[] value->commonTopics[i];
    delete[] value->commonTopics;
    delete value;
}

int create(void** output, const fnet_conn_settings_t* settings)
{
    mock->record("create");
    mock->settings = *settings;
    *output = mock;
    require(std::string(settings->clientId) == "client-id", "wrong connection client");
    if (mock->delayed_connect) {
        const auto captured = mock->settings;
        mock->callback_thread = std::thread([captured]() {
            std::this_thread::sleep_for(5ms);
            captured.statusCallback(FNET_CONN_STATUS_CONNECTED, captured.statusCallbackData);
        });
    } else if (mock->connect_on_create) {
        settings->statusCallback(FNET_CONN_STATUS_CONNECTED, settings->statusCallbackData);
    }
    return mock->create_code;
}

void stop(void* connection)
{
    require(connection == mock, "wrong stop handle");
    mock->record("stop");
    mock->settings.statusCallback(FNET_CONN_STATUS_DISCONNECTED, mock->settings.statusCallbackData);
}

void free_connection(void* connection)
{
    require(connection == mock && !mock->library.expired(), "SDK library released before connection");
    if (mock->callback_thread.joinable()) mock->callback_thread.join();
    mock->record("free");
    ++mock->free_count;
}

int subscribe(void* connection, const fnet_conn_subscribe_data_t* data)
{
    require(connection == mock && data->topicCnt == 3, "wrong subscription handle or topics");
    mock->record("subscribe");
    std::vector<std::string> topics;
    for (int i = 0; i < data->topicCnt; ++i) topics.emplace_back(data->topics[i]);
    require(topics == std::vector<std::string>{"common-topic", "device-topic", "user-topic"}, "wrong subscription names");
    return mock->subscribe_code;
}

int sync(void* connection, const fnet_conn_write_data_t* data)
{
    if (data->type == FNET_CONN_WRITE_JOB_CTRL) {
        require(connection == mock && data->qos == 1 && std::string(data->topic) == "device-topic", "wrong job routing");
        const auto& job = *static_cast<const fnet_job_ctrl_t*>(data->data);
        require(std::string(job.jobId) == "job-1" && std::string(job.action) == "pause", "wrong job payload");
        ++mock->job_controls;
        if (mock->disconnect_on_publish) mock->settings.statusCallback(FNET_CONN_STATUS_DISCONNECTED, mock->settings.statusCallbackData);
        if (mock->change_owner_on_publish) mock->changed_owner.store(true);
        return mock->publish_code;
    }
    require(connection == mock && data->type == FNET_CONN_WRITE_SYNC_LOGIN && data->qos == 1, "wrong sync command");
    require(std::string(data->topic) == "user-topic" && std::string(static_cast<const char*>(data->data)) == "client-id", "wrong sync session");
    mock->record("sync");
    return mock->sync_code;
}

int publish(void* connection, const fnet_conn_write_multi_data_t*, fnet_conn_write_multi_result_t** output)
{
    require(connection == mock, "wrong publish handle");
    mock->record("publish");
    ++mock->publish_count;
    *output = new fnet_conn_write_multi_result_t{nullptr, 0};
    if (mock->disconnect_on_publish)
        mock->settings.statusCallback(FNET_CONN_STATUS_DISCONNECTED, mock->settings.statusCallbackData);
    if (mock->change_owner_on_publish) mock->changed_owner.store(true);
    return mock->publish_code;
}

char* allocate_string(const char* value, int length)
{
    if (mock->fail_allocation) return nullptr;
    require(length == static_cast<int>(std::strlen(value)), "wrong allocated client length");
    return copy(value);
}

void free_string(const char* value) { ++mock->strings_freed; delete[] value; }
void free_login(const fnet_sync_login_info_t* value)
{
    ++mock->login_freed; delete[] value->clientType; delete[] value->clientId; delete value;
}
void free_bind(const fnet_sync_bind_info_t* value)
{
    ++mock->bind_freed; delete[] value->fromClinetId; delete[] value->devId; delete value;
}
void free_online(const fnet_sync_online_info_t* value)
{
    ++mock->online_freed; delete[] value->uid; delete[] value->devId; delete value;
}
void free_detail(const fnet_dev_detail_t* value)
{
    ++mock->detail_freed; delete[] value->devId; delete[] value->status; delete[] value->firmwareVersion; delete value;
}

FlashforgeConnectionApi api()
{
    return {config, free_config, create, stop, free_connection, subscribe, sync, publish,
        allocate_string, free_string, free_login, free_bind, free_online, free_detail};
}

CloudConnectionSessionProvider provider()
{
    return []() -> std::optional<CloudConnectionSession> {
        if (mock->throw_provider.load()) throw std::runtime_error("private-token");
        if (mock->pause_provider.load()) {
            mock->provider_entered.store(true);
            std::this_thread::sleep_for(20ms);
        }
        return CloudConnectionSession(mock->changed_client.load() ? "other-client" : "client-id",
            mock->rotated_token.load() ? "rotated-token" : "private-token",
            std::chrono::steady_clock::now() + (mock->expired.load() ? -1s : 1h), mock->changed_owner.load() ? 2 : 1);
    };
}

int send_checked(FlashforgeCloudConnection& connection, const char* topic = "device-topic")
{
    fnet_clound_job_data_t job{};
    const void* data = &job;
    fnet_conn_write_multi_data_t write{FNET_CONN_WRITE_START_CLOUND_JOB, &data, &topic, 1, 1, 1};
    fnet_conn_write_multi_result_t* output = nullptr;
    const int code = FlashforgeCloudConnection::send_checked(connection.workflow_context(), &write, &output);
    delete output;
    return code;
}

void deliver(fnet_conn_read_data_type_t type, void* payload)
{
    fnet_conn_read_data_t read{type, payload, copy("device-topic")};
    mock->settings.readCallback(&read, mock->settings.readCallbackData);
}

template<class Configure, class Check>
void scenario(Configure configure, Check check)
{
    Mock state;
    mock = &state;
    auto owner = std::make_shared<int>(1);
    state.library = owner;
    configure(state);
    {
        FlashforgeCloudConnection connection(api(), owner, provider());
        check(state, connection, owner);
        require(connection.message().find("private-token") == std::string::npos, "credential leaked in message");
    }
    require(state.free_count.load() <= 2, "connection was freed too many times");
}
}

int main()
{
    try {
        scenario([](auto&) {}, [](auto& m, auto& c, auto&) {
            require(c.open({"device-topic"}, 20ms), "control open failed");
            require(c.control_job("other-device", "job-1", "pause") == FNET_ERROR && m.job_controls == 0, "unsubscribed control escaped");
            require(c.control_job("device-topic", "", "pause") == FNET_ERROR && m.job_controls == 0, "missing job accepted");
            require(c.control_job("device-topic", "job-1", "arbitrary") == FNET_ERROR && m.job_controls == 0, "unknown action accepted");
            require(c.control_job("device-topic", "job-1", "pause") == FNET_OK && m.job_controls == 1, "control not dispatched exactly once");
            m.changed_owner.store(true);
            require(c.control_job("device-topic", "job-1", "pause") == FNET_UNAUTHORIZED && m.job_controls == 1, "old account controlled printer");
        });
        scenario([](auto& m) { m.disconnect_on_publish = true; }, [](auto& m, auto& c, auto&) {
            require(c.open({"device-topic"}, 20ms), "control open failed");
            require(c.control_job("device-topic", "job-1", "pause") == FNET_CONN_SEND_ERROR && m.job_controls == 1, "disconnect claimed control success or replayed it");
            m.settings.statusCallback(FNET_CONN_STATUS_CONNECTED, m.settings.statusCallbackData);
            require(c.prepare() && m.job_controls == 1, "reconnect replayed a control");
        });
        scenario([](auto& m) { m.change_owner_on_publish = true; }, [](auto& m, auto& c, auto&) {
            require(c.open({"device-topic"}, 20ms), "control open failed");
            require(c.control_job("device-topic", "job-1", "pause") == FNET_CONN_SEND_ERROR && m.job_controls == 1, "account replacement claimed control success");
        });
        scenario([](auto&) {}, [](auto& m, auto& c, auto& owner) {
            require(c.open({"device-topic"}, 20ms) && c.state() == CloudConnectionState::ready, "ready callback not observed");
            require(send_checked(c) == FNET_OK && m.publish_count == 1, "manual publish failed");
            owner.reset();
            require(!m.library.expired(), "borrowed SDK owner not retained");
            c.close();
            require(m.calls == std::vector<std::string>{"config", "create", "subscribe", "sync", "publish", "stop", "free"}, "wrong lifecycle order");
            require(m.configs_freed == 1 && m.free_count == 1 && !m.library.expired(), "connection ownership failure");
        });
        scenario([](auto& m) { m.delayed_connect = true; }, [](auto&, auto& c, auto&) {
            require(c.open({"device-topic"}, 500ms), "delayed readiness failed");
        });
        scenario([](auto& m) { m.connect_on_create = false; }, [](auto& m, auto& c, auto&) {
            require(!c.open({"device-topic"}, 1ms) && c.state() == CloudConnectionState::closed && m.free_count == 1, "timeout invented readiness");
        });
        scenario([](auto& m) { m.config_code = FNET_UNAUTHORIZED; }, [](auto& m, auto& c, auto&) {
            require(!c.open({"device-topic"}, 20ms) && c.state() == CloudConnectionState::authentication_required, "unauthorized config accepted");
            require(m.calls == std::vector<std::string>{"config"} && m.configs_freed == 1, "unauthorized config leaked");
        });
        scenario([](auto& m) { m.expired.store(true); }, [](auto& m, auto& c, auto&) {
            require(!c.open({"device-topic"}, 20ms) && m.calls.empty(), "expired session was used");
        });
        scenario([](auto& m) { m.create_code = FNET_ERROR; }, [](auto& m, auto& c, auto&) {
            require(!c.open({"device-topic"}, 20ms) && m.free_count == 1, "partial create leaked");
        });
        scenario([](auto& m) { m.malformed_config = true; }, [](auto& m, auto& c, auto&) {
            require(!c.open({"device-topic"}, 20ms) && m.configs_freed == 1, "invalid config accepted");
        });
        scenario([](auto& m) { m.subscribe_code = FNET_ERROR; }, [](auto&, auto& c, auto&) {
            require(!c.open({"device-topic"}, 20ms), "failed subscription accepted");
        });
        scenario([](auto& m) { m.sync_code = FNET_ERROR; }, [](auto&, auto& c, auto&) {
            require(!c.open({"device-topic"}, 20ms), "failed sync accepted");
        });
        scenario([](auto&) {}, [](auto& m, auto& c, auto&) {
            require(c.open({"device-topic"}, 20ms), "initial open failed");
            m.settings.statusCallback(FNET_CONN_STATUS_DISCONNECTED, m.settings.statusCallbackData);
            require(send_checked(c) == FNET_CONN_SEND_ERROR && m.publish_count == 0, "publish attempted while disconnected");
            m.settings.statusCallback(FNET_CONN_STATUS_CONNECTED, m.settings.statusCallbackData);
            require(c.state() != CloudConnectionState::ready && m.publish_count == 0, "reconnect replayed command");
            require(c.prepare() && send_checked(c) == FNET_OK && m.publish_count == 1, "explicit reconnect preparation failed");
        });
        scenario([](auto& m) { m.disconnect_on_publish = true; }, [](auto& m, auto& c, auto&) {
            require(c.open({"device-topic"}, 20ms) && send_checked(c) == FNET_CONN_SEND_ERROR, "disconnect during publish claimed success");
            require(m.publish_count == 1, "uncertain publish was retried");
        });
        scenario([](auto&) {}, [](auto& m, auto& c, auto&) {
            require(c.open({"device-topic"}, 20ms), "initial open failed");
            require(send_checked(c, "other-device-topic") == FNET_ERROR && m.publish_count == 0, "unselected device was published");
            m.expired.store(true);
            require(send_checked(c) == FNET_UNAUTHORIZED && m.publish_count == 0, "expired publish session accepted");
        });
        scenario([](auto&) {}, [](auto& m, auto& c, auto&) {
            require(c.open({"device-topic"}, 20ms), "initial open failed");
            m.changed_client.store(true);
            require(send_checked(c) == FNET_UNAUTHORIZED && m.publish_count == 0, "cross-session publish attempted");
        });
        scenario([](auto&) {}, [](auto& m, auto& c, auto&) {
            require(c.open({"device-topic"}, 20ms), "initial open failed");
            m.changed_owner.store(true); // Same client identifier, different login/account owner.
            require(send_checked(c) == FNET_UNAUTHORIZED && m.publish_count == 0, "changed account retained old topics");
            require(!c.prepare() && c.state() == CloudConnectionState::authentication_required, "changed owner remained ready");
            m.settings.statusCallback(FNET_CONN_STATUS_CONNECTED, m.settings.statusCallbackData);
            require(c.state() == CloudConnectionState::authentication_required, "callback accepted changed account owner");
            require(c.open({"device-topic"}, 20ms) && send_checked(c) == FNET_OK, "explicit reopen for new owner failed");
        });
        scenario([](auto& m) { m.change_owner_on_publish = true; }, [](auto& m, auto& c, auto&) {
            require(c.open({"device-topic"}, 20ms) && send_checked(c) == FNET_CONN_SEND_ERROR && m.publish_count == 1,
                    "account changed during publish was reported successful");
        });
        scenario([](auto&) {}, [](auto& m, auto& c, auto&) {
            require(c.open({"device-topic"}, 20ms), "initial open failed");
            m.rotated_token.store(true); // Token refresh does not replace account ownership.
            const char* replacement = nullptr;
            require(m.settings.updateCallback(&replacement, m.settings.updateCallbackData) == 0, "token rotation invalidated owner");
            free_string(replacement);
            require(c.prepare() && send_checked(c) == FNET_OK, "same-owner token refresh failed");
        });
        scenario([](auto&) {}, [](auto& m, auto& c, auto&) {
            require(c.open({"device-topic"}, 20ms), "initial open failed");
            const char* replacement = nullptr;
            require(m.settings.updateCallback(&replacement, m.settings.updateCallbackData) == 0 && replacement && std::string(replacement) == "client-id", "callback session refresh failed");
            free_string(replacement); // The SDK would consume/free this string.
            require(c.state() != CloudConnectionState::ready && c.prepare(), "renewed session became ready without subscriptions");
            m.config_code = FNET_UNAUTHORIZED;
            require(m.settings.updateCallback(&replacement, m.settings.updateCallbackData) != 0 && !replacement, "unauthorized callback accepted");
            require(c.state() == CloudConnectionState::authentication_required && m.configs_freed == 3, "callback auth fault not surfaced");
        });
        scenario([](auto&) {}, [](auto& m, auto& c, auto&) {
            require(c.open({"device-topic"}, 20ms), "initial open failed");
            m.throw_provider.store(true);
            const char* replacement = nullptr;
            require(m.settings.updateCallback(&replacement, m.settings.updateCallbackData) != 0 && c.state() == CloudConnectionState::fault, "exception crossed update callback");
        });
        scenario([](auto&) {}, [](auto& m, auto& c, auto&) {
            require(c.open({"device-topic"}, 20ms), "initial open failed");
            m.fail_allocation = true;
            const char* replacement = nullptr;
            require(m.settings.updateCallback(&replacement, m.settings.updateCallbackData) != 0 && !replacement, "failed callback allocation accepted");
        });
        scenario([](auto&) {}, [](auto& m, auto& c, auto&) {
            require(c.open({"device-topic"}, 20ms), "initial open failed");
            deliver(FNET_CONN_READ_UPDATE_NOTIFY, copy("{\"content\":{\"5m\":{\"en\":\"Update available\"}}}"));
            deliver(FNET_CONN_READ_SYNC_ONLINE, new fnet_sync_online_info_t{copy("uid"), copy("dev-1")});
            deliver(FNET_CONN_READ_SYNC_BIND_DEVICE, new fnet_sync_bind_info_t{copy("client-id"), copy("dev-1")});
            auto* detail = new fnet_dev_detail_t{};
            detail->devId = copy("dev-1"); detail->status = copy("ready"); detail->firmwareVersion = copy("1.0.0");
            deliver(FNET_CONN_READ_DEVICE_DETAIL, detail);
            const auto events = c.drain_events();
            require(events.size() == 4 && events[1].device_id == "dev-1" && events[3].firmware_version == "1.0.0", "device event data lost");
            require(c.drain_events().empty() && m.strings_freed == 5 && m.online_freed == 1 && m.bind_freed == 1 && m.detail_freed == 1, "read callback buffers leaked");
            deliver(FNET_CONN_READ_SYNC_LOGIN, new fnet_sync_login_info_t{copy("pc"), copy("another-client")});
            require(c.state() == CloudConnectionState::authentication_required && m.login_freed == 1, "vendor simultaneous-PC restriction ignored");
        });
        scenario([](auto&) {}, [](auto& m, auto& c, auto&) {
            require(c.open({"device-topic"}, 20ms), "initial open failed");
            m.pause_provider.store(true);
            const auto settings = m.settings;
            m.callback_thread = std::thread([settings]() {
                const char* replacement = nullptr;
                settings.updateCallback(&replacement, settings.updateCallbackData);
                if (replacement) free_string(replacement);
            });
            const auto limit = std::chrono::steady_clock::now() + 1s;
            while (!m.provider_entered.load() && std::chrono::steady_clock::now() < limit) std::this_thread::sleep_for(1ms);
            require(m.provider_entered.load(), "callback test did not start");
            c.close();
            require(m.free_count == 1 && !m.callback_thread.joinable() && c.state() == CloudConnectionState::closed, "close did not quiesce callbacks");
        });
        Mock unsupported;
        mock = &unsupported;
        auto incomplete = api(); incomplete.send_multi = nullptr;
        FlashforgeCloudConnection connection(incomplete, std::make_shared<int>(1), provider());
        require(!connection.open({"device-topic"}, 20ms) && connection.state() == CloudConnectionState::unsupported && unsupported.calls.empty(), "missing SDK capability did not fall back");
        std::cout << "Simulated cloud connection lifecycle and fault checks passed; no network calls.\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "Connection test failed: " << error.what() << '\n';
        return 1;
    }
}
