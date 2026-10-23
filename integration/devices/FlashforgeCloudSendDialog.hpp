#pragma once
#include <string>
class wxWindow;
namespace Slic3r { namespace GUI {
void show_flashforge_cloud_send(wxWindow* parent, const std::string& qualified_id,
    bool start_printing = false, bool* request_started = nullptr);
}}
