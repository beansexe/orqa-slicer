#include "BambuHomepageStatus.hpp"

#include <iostream>
#include <nlohmann/json.hpp>
#include <stdexcept>
#include <string>

namespace {
void check(bool condition, const char* description)
{
    if (!condition) throw std::runtime_error(description);
}
}

int main()
{
    const auto signed_in = nlohmann::json::parse(
        Slic3r::build_bambu_homepage_status(true, "account", "Display name", "https://example.invalid/avatar"));
    check(signed_in == nlohmann::json{{"command", "studio_bambu_userlogin"},
                                    {"data", {{"name", "Display name"}, {"avatar", "https://example.invalid/avatar"}}}},
          "signed-in status uses the Bambu command, nickname and display-only fields");

    const auto user_name = nlohmann::json::parse(Slic3r::build_bambu_homepage_status(true, "account", "", ""));
    check(user_name["data"]["name"] == "account", "account name is the fallback when the nickname is absent");

    const auto restored = nlohmann::json::parse(Slic3r::build_bambu_homepage_status(true, "", "", ""));
    check(restored["command"] == "studio_bambu_userlogin" && restored["data"]["name"] == "Bambu Lab" &&
              restored["data"]["avatar"] == "",
          "a restored session remains signed in before profile fields are available");

    const std::string escaped_name = "Nickname \"quoted\" \\ newline\n\t\x01";
    const std::string escaped_avatar = "https://example.invalid/\"avatar\\?x=1&y=2\n";
    const auto escaped = nlohmann::json::parse(
        Slic3r::build_bambu_homepage_status(true, "unused", escaped_name, escaped_avatar));
    check(escaped["data"]["name"] == escaped_name && escaped["data"]["avatar"] == escaped_avatar,
          "names and avatar text round-trip through JSON escaping");

    const auto signed_out = nlohmann::json::parse(
        Slic3r::build_bambu_homepage_status(false, "previous account", "previous nickname", "previous avatar"));
    check(signed_out == nlohmann::json{{"command", "studio_bambu_useroffline"}},
          "signed-out status contains no stale profile fields or another provider's command");
    check(signed_in["command"] != "orca_userlogin" && signed_in["command"] != "studio_userlogin" &&
              signed_out["command"] != "orca_useroffline" && signed_out["command"] != "studio_useroffline",
          "Bambu status cannot update an Orca or legacy shared account row");

    std::cout << "All Bambu homepage status checks passed. No accounts or network were accessed.\n";
}
