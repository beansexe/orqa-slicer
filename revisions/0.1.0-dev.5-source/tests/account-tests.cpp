#include "FlashforgeAccount.hpp"
#include <iostream>
#include <stdexcept>
#include <string>

using Slic3r::FlashforgeAccount;
namespace {
int login_result = FNET_OK, refresh_result = FNET_OK, devices_result = FNET_OK;
int freed_tokens = 0, freed_devices = 0, logout_calls = 0;
int refresh_calls = 0;
bool unauthorized_once = false;
std::string received_password;
fnet_token_data_t token{3600, const_cast<char*>("access-test"), const_cast<char*>("refresh-test")};
fnet_wan_dev_info_t device{};
int login(const char*, const char* password, const char*, fnet_token_data_t** out, char**, int)
{ received_password = password; *out = &token; return login_result; }
int refresh(const char*, const char*, fnet_token_data_t** out, char**, int)
{ ++refresh_calls; *out = &token; return refresh_result; }
void free_token(fnet_token_data_t*) { ++freed_tokens; }
void free_string(const char*) {}
int logout(const char*, int) { ++logout_calls; return FNET_ERROR; }
int devices(const char* client, const char*, long long, long long, fnet_wan_dev_info_t** out, int* count, int)
{
    if (std::string(client).size() != 35 || std::string(client).substr(0, 3) != "pc_")
        throw std::runtime_error("client id must match published desktop format");
    *out = &device; *count = 1;
    if (unauthorized_once) { unauthorized_once = false; return FNET_UNAUTHORIZED; }
    return devices_result;
}
void free_devices(fnet_wan_dev_info_t*, int) { ++freed_devices; }
void check(bool condition, const char* description)
{ if (!condition) throw std::runtime_error(description); }
}
int main(int argc, char** argv)
{
    if (argc == 2) {
        std::cout << "Plugin ABI version: " << FlashforgeAccount::plugin_version(argv[1]) << '\n';
        return 0;
    }
    if (argc == 3) {
        auto account = FlashforgeAccount::load(argv[1], argv[2]);
        std::cout << account->message() << '\n';
        return account->state() == FlashforgeAccount::State::signed_out ? 0 : 1;
    }
    device.devId = const_cast<char*>("printer-test");
    device.name = const_cast<char*>("My 5M");
    device.model = const_cast<char*>("Adventurer 5M");
    Slic3r::FlashforgeApi api{login, refresh, free_token, free_string, logout, devices, free_devices};
    FlashforgeAccount first(api), second(api), unavailable({});
    check(!unavailable.login("user", "password"), "missing plugin must not call login");
    check(!first.login("", ""), "empty input rejected");
    check(first.login("user", "literal-password"), "login and printer retrieval");
    check(received_password == "literal-password", "password passed unchanged");
    check(first.devices().at(0).id == "flashforge:printer-test", "qualified printer id");
    check(freed_tokens == 1 && freed_devices == 1, "vendor buffers freed");
    check(!first.login("other", "other"), "no silent account switch");
    check(second.restore("saved-refresh"), "restart uses refresh token");
    unauthorized_once = true;
    int refresh_before = refresh_calls;
    check(first.refresh_devices() && refresh_calls == refresh_before + 1,
          "expired access token transparently refreshed once");
    check(first.session_secret() == "refresh-test", "refresh secret available for keychain");
    devices_result = FNET_ERROR;
    check(!first.refresh_devices() && first.devices().empty(), "offline clears stale device list");
    check(first.state() == FlashforgeAccount::State::signed_in, "offline preserves session");
    first.logout();
    check(first.state() == FlashforgeAccount::State::signed_out && first.session_secret().empty(), "offline logout clears local session");
    check(second.state() == FlashforgeAccount::State::signed_in, "logout isolated from another account");
    check(logout_calls == 1, "only one cloud logout");
    devices_result = FNET_UNAUTHORIZED;
    check(!second.refresh_devices() && second.session_secret().empty(), "expired device auth clears session");
    refresh_result = FNET_INVALID_VALIDATION;
    check(!second.restore("expired-refresh"), "invalid refresh rejected");
    refresh_result = FNET_ERROR;
    check(!second.restore("offline-refresh") && second.session_secret() == "offline-refresh",
          "offline restoration retains remembered token for retry");
    login_result = FNET_INVALID_VALIDATION;
    check(!first.login("user", "wrong"), "invalid password rejected");
    login_result = FNET_OK;
    token.accessToken = nullptr;
    check(!first.login("user", "password"), "malformed success rejected");
    auto missing = FlashforgeAccount::load("relative-plugin.dll", "missing.dat");
    check(missing->state() == FlashforgeAccount::State::unavailable, "relative plugin not loaded");
    std::cout << "All account adapter checks passed. No cloud requests were made.\n";
}
