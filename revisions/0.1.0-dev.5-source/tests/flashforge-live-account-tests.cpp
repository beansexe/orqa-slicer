#include "FlashforgeAccount.hpp"
#include "FlashNetwork.h"
#include "printing/FlashforgeCloudPrint.hpp"
#include <atomic>
#include <cmath>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>
namespace {
fnet_token_data_t token{3600, const_cast<char*>("test-access"), const_cast<char*>("test-refresh")};
fnet_wan_dev_info_t printer{};
fnet_dev_product_t product{};
fnet_dev_detail_t detail{};
int detail_result = FNET_OK, detail_calls = 0, released_details = 0, released_products = 0, refresh_calls = 0;
bool unauthorized_once = false;
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
    detail.devId = printer.devId; detail.nozzleCnt = 1;
    detail.status = const_cast<char*>("ready"); detail.firmwareVersion = const_cast<char*>("test-firmware");
    detail.rightTemp = 23.; detail.rightTargetTemp = 210.; detail.platTemp = 25.; detail.platTargetTemp = 60.; detail.printProgress = .5;
    Slic3r::FlashforgeApi api{login, refresh, free_token, free_string, logout, devices, free_devices, get_detail, free_product, free_detail};
    Slic3r::FlashforgeAccount account(api);
    check(!account.refresh_status("flashforge:test-printer") && detail_calls == 0, "signed-out lookup must not call SDK");
    check(account.login("test-user", "test-password"), "mock login");
    check(!account.refresh_status("flashforge:other-printer") && detail_calls == 0, "unowned identity must not call SDK");
    check(account.refresh_status("flashforge:test-printer"), "live status returned");
    check(account.live_status() && account.live_status()->nozzle_temperature == 23. && account.live_status()->progress == .5, "telemetry copied");
    check(released_details == 1 && released_products == 1, "all SDK buffers released");
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
    account.logout();
    check(!account.live_status(), "logout clears telemetry");
    std::cout << "Live account ownership, authorization, telemetry and cleanup tests passed; no network calls.\n";
}
