#pragma once

#include <nlohmann/json.hpp>
#include <string>

namespace Slic3r {

// Homepage status uses the native session state, independently of login configuration.
inline std::string build_bambu_homepage_status(bool signed_in, const std::string& user_name,
                                             const std::string& nickname, const std::string& avatar)
{
    if (!signed_in)
        return nlohmann::json{{"command", "studio_bambu_useroffline"}}.dump();

    std::string name = nickname.empty() ? user_name : nickname;
    if (name.empty()) name = "Bambu Lab";
    return nlohmann::json{{"command", "studio_bambu_userlogin"},
                          {"data", {{"name", name}, {"avatar", avatar}}}}.dump();
}

} // namespace Slic3r
