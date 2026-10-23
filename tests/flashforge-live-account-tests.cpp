#include "FlashforgeAccount.hpp"
#include "FlashNetwork.h"
#include "printing/FlashforgeCloudPrint.hpp"
#include "printing/FlashforgeCloudConnection.hpp"
#include <atomic>
#include <cmath>
#include <iostream>
#include <limits>
#include <memory>
#include <stdexcept>
#include <string>
namespace {
fnet_token_data_t token{3600, const_cast<char*>("test-access"), const_cast<char*>("test-refresh")};
fnet_wan_dev_info_t printer{};
fnet_dev_product_t product{};
fnet_dev_detail_t detail{};
int detail_result = FNET_OK, detail_calls = 0, released_details = 0, released_products = 0, refresh_calls = 0;
bool unauthorized_once = false;
bool change_job_on_connect = false;
int job_commands = 0;
fnet_conn_settings_t connection_settings{};
fnet_mqtt_config_t mqtt_config{const_cast<char*>("test-user-topic"), nullptr, 0};
int config(const char*, const char*, fnet_mqtt_config_t** out, int) { *out = &mqtt_config; return FNET_OK; }
void free_config(fnet_mqtt_config_t*) {}
int create(void** out, const fnet_conn_settings_t* settings) {
    connection_settings = *settings; *out = &connection_settings;
    if (change_job_on_connect) detail.jobId = const_cast<char*>("replacement-job");
    settings->statusCallback(FNET_CONN_STATUS_CONNECTED, settings->statusCallbackData);
    return FNET_OK;
}
void stop(void*) {}
void free_connection(void*) {}
int subscribe(void*, const fnet_conn_subscribe_data_t*) { return FNET_OK; }
int send(void*, const fnet_conn_write_data_t* data) {
    if (data->type == FNET_CONN_WRITE_JOB_CTRL) {
        const auto& job = *static_cast<const fnet_job_ctrl_t*>(data->data);
        if (std::string(data->topic) != "test/topic" || std::string(job.jobId) != "test-job") throw std::runtime_error("wrong job route");
        ++job_commands;
    }
    return FNET_OK;
}
int send_multi(void*, const fnet_conn_write_multi_data_t*, fnet_conn_write_multi_result_t**) { return FNET_ERROR; }
char* allocate_string(const char*, int) { return nullptr; }
void free_login(const fnet_sync_login_info_t*) {}
void free_bind(const fnet_sync_bind_info_t*) {}
void free_online(const fnet_sync_online_info_t*) {}
int login(const char*, const char*, const char*, fnet_token_data_t** out, char**, int) { *out = &token; return FNET_OK; }
int refresh(const char*, const char*, fnet_token_data_t** out, char**, int) { ++refresh_calls; *out = &token; return FNET_OK; }
void free_token(fnet_token_data_t*) {}
void free_string(const char*) {}
int logout(const char*, int) { return FNET_OK; }
int devices(const char*, const char*, long long, long long, fnet_wan_dev_info_t** out, int* count, int) { *out = &printer; *count = 1; return FNET_OK; }
void free_devices(fnet_wan_dev_info_t*, int) {}
int get_detail(const char*, const char*, const char*, fnet_dev_product_t** p, fnet_dev_detail_t** d, int) {
    ++detail_calls; *p = &product; *d = &detail;
    if (unauthorized_once) { unauthorized_once = false; return FNET_UNAUTHORIZED; }
    return detail_result;
}
void free_product(fnet_dev_product_t*) { ++released_products; }
void free_detail(const fnet_dev_detail_t*) { ++released_details; }
void check(bool condition, const char* message) { if (!condition) throw std::runtime_error(message); }
}
int main() {
    printer.devId = const_cast<char*>("test-printer");
    printer.model = const_cast<char*>("Adventurer 5M");
    printer.serialNumber = const_cast<char*>("test-serial");
    printer.devTopic = const_cast<char*>("test/topic");
    printer.updateInfo.status = const_cast<char*>("device");
    printer.updateInfo.title = const_cast<char*>("Firmware update");
    detail.devId = printer.devId; detail.nozzleCnt = 1;
    detail.status = const_cast<char*>("ready"); detail.firmwareVersion = const_cast<char*>("test-firmware");
    detail.rightTemp = 23.; detail.rightTargetTemp = 210.; detail.platTemp = 25.; detail.platTargetTemp = 60.; detail.printProgress = .5;
    Slic3r::FlashforgeApi api{login, refresh, free_token, free_string, logout, devices, free_devices, get_detail, free_product, free_detail};
    Slic3r::FlashforgeAccount account(api);
    check(!account.refresh_status("flashforge:test-printer") && detail_calls == 0, "signed-out lookup must not call SDK");
    check(account.login("test-user", "test-password"), "mock login");
    check(account.devices()[0].firmware_update_available && account.devices()[0].firmware_update_title == "Firmware update", "copy explicit firmware update before SDK list is freed");
    printer.updateInfo.status = const_cast<char*>("studio");
    check(account.refresh_devices() && !account.devices()[0].firmware_update_available, "software update is not printer firmware");
    check(!account.refresh_status("flashforge:other-printer") && detail_calls == 0, "unowned identity must not call SDK");
    check(account.refresh_status("flashforge:test-printer"), "live status returned");
    check(account.live_status() && account.live_status()->nozzle_temperature == 23. && account.live_status()->progress == .5, "telemetry copied");
    check(released_details == 1 && released_products == 1, "all SDK buffers released");
    double unused_nozzle_array[] = {0.};
    detail.nozzleTemps = unused_nozzle_array; detail.nozzleTargetTemps = unused_nozzle_array;
    check(account.refresh_status("flashforge:test-printer") && account.live_status()->nozzle_temperature == 23. &&
          account.live_status()->nozzle_target == 210., "Adventurer 5M must ignore unused zero-filled nozzle arrays");
    detail.nozzleTemps = nullptr; detail.nozzleTargetTemps = nullptr;
    detail.devId = const_cast<char*>("wrong-printer");
    check(!account.refresh_status("flashforge:test-printer") && !account.live_status(), "wrong returned identity rejected");
    detail.devId = printer.devId;
    detail.rightTemp = std::numeric_limits<double>::quiet_NaN(); detail.printProgress = 2.;
    check(account.refresh_status("flashforge:test-printer") && !account.live_status()->nozzle_temperature && !account.live_status()->progress, "invalid numbers omitted");
    unauthorized_once = true;
    check(account.refresh_status("flashforge:test-printer") && refresh_calls == 1, "unauthorized status refreshes once");
    detail_result = FNET_ERROR;
    check(!account.refresh_status("flashforge:test-printer") && !account.live_status(), "failed status clears stale telemetry");
    check(released_details == detail_calls && released_products == detail_calls, "buffers released on error and retry");
    std::atomic<bool> cancelled(false);
    Slic3r::VendorPrinting::CloudPrintRequest request;
    check(account.send_cloud(request, {}, cancelled).outcome == Slic3r::VendorPrinting::CloudPrintOutcome::rejected, "missing cloud API rejects before upload");
    // Races and stale identity are difficult to exercise reliably with hardware.
    Slic3r::VendorPrinting::FlashforgeConnectionApi connection_api{config, free_config, create, stop, free_connection,
        subscribe, send, send_multi, allocate_string, free_string, free_login, free_bind, free_online, free_detail};
    Slic3r::FlashforgeAccount controlled(api, {}, connection_api, std::make_shared<int>(1));
    check(controlled.login("test-user", "test-password"), "control mock login");
    detail_result = FNET_OK;
    detail.jobId = const_cast<char*>("test-job"); detail.status = const_cast<char*>("printing");
    using Action = Slic3r::FlashforgeJobAction;
    check(!controlled.control_job("flashforge:other-printer", "test-job", Action::cancel) && job_commands == 0, "unowned job control rejected");
    check(!controlled.control_job("flashforge:test-printer", "old-job", Action::cancel) && job_commands == 0, "stale job control rejected");
    check(!controlled.control_job("flashforge:test-printer", "test-job", Action::resume) && job_commands == 0, "wrong job state rejected");
    change_job_on_connect = true;
    check(!controlled.control_job("flashforge:test-printer", "test-job", Action::cancel) && job_commands == 0, "job replaced during connection was controlled");
    change_job_on_connect = false; detail.jobId = const_cast<char*>("test-job");
    check(controlled.control_job("flashforge:test-printer", "test-job", Action::pause) && job_commands == 1 && !controlled.live_status(), "control not sent once or optimistic state retained");
    account.logout();
    check(!account.live_status(), "logout clears telemetry");
    std::cout << "Live account ownership, authorization, telemetry and cleanup tests passed; no network calls.\n";
}
