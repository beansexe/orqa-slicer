#pragma once
class wxWindow;
namespace Slic3r { namespace GUI {
// UI-thread mailbox. Lifetime is owned by the editor frame.
void start_solidworks_bridge(wxWindow* owner);
void stop_solidworks_bridge();
}}
