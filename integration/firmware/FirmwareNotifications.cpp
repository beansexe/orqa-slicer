#include "FirmwareNotifications.hpp"
#include "FirmwareNotices.hpp"
#include "FirmwareProviderData.hpp"
#include "../FlashforgeAccount.hpp"
#include "../printers/PrinterCatalog.hpp"
#include "slic3r/GUI/DeviceManager.hpp"
#include "slic3r/GUI/GUI_App.hpp"
#include "slic3r/GUI/I18N.hpp"
#include "slic3r/GUI/NotificationManager.hpp"
#include "slic3r/GUI/Plater.hpp"
#include "slic3r/Utils/CloudProvider.hpp"
#include "slic3r/Utils/NetworkAgent.hpp"
#include <wx/string.h>
#include <wx/strconv.h>
#include <chrono>
#include <string>
#include <vector>

namespace Slic3r { namespace GUI {
namespace {
FirmwareNotices notices;
wxString display(std::string value)
{
    for (char& c : value) if (static_cast<unsigned char>(c) < 32 || static_cast<unsigned char>(c) == 127) c = ' ';
    return wxString::FromUTF8(value).Left(240);
}
void announce(PrinterProvider provider, const std::string& name)
{
    auto* plater = wxGetApp().plater();
    if (!plater || !plater->get_notification_manager()) return;
    const auto now = FirmwareNotices::Clock::now();
    for (const auto& notice : notices.pending(now)) {
        if (notice.provider != provider) continue;
        wxString message = _L("Printer firmware update available") + "\n" + display(name);
        if (!notice.update.available_version.empty()) message += "\n" + display(notice.update.available_version);
        if (!notice.update.title.empty()) message += "\n" + display(notice.update.title);
        message += "\n" + (provider == PrinterProvider::bambu ?
            _L("Open Device > Update to review it.") : _L("Review this update in the Flashforge printer or vendor slicer."));
        plater->get_notification_manager()->push_notification(NotificationType::CustomNotification,
            NotificationManager::NotificationLevel::ImportantNotificationLevel, message.ToStdString(wxConvUTF8));
        notices.mark_announced(notice, now);
    }
}
}

void observe_bambu_firmware(MachineObject& machine, const std::string& provider, const std::string& message)
{
    if (wxGetApp().is_closing() || provider != BBL_CLOUD_PROVIDER) return;
    auto* agent = wxGetApp().getAgent();
    const bool connected = agent && wxGetApp().is_user_login(BBL_CLOUD_PROVIDER) && agent->is_server_connected(BBL_CLOUD_PROVIDER);
    notices.set_account_readiness(PrinterProvider::bambu, connected ? AccountReadiness::signed_in : AccountReadiness::unavailable);
    if (!connected) return;
    const auto update = bambu_firmware_update(machine.get_dev_id(), message);
    if (!update) return;
    notices.replace(PrinterProvider::bambu, {*update}, FirmwareNotices::Clock::now(), std::chrono::seconds(60));
    announce(PrinterProvider::bambu, machine.get_dev_name());
}

void observe_flashforge_firmware(bool signed_in, bool fresh, const std::vector<FlashforgeDevice>& devices)
{
    if (wxGetApp().is_closing()) return;
    notices.set_account_readiness(PrinterProvider::flashforge, signed_in ? AccountReadiness::signed_in : AccountReadiness::signed_out);
    if (!signed_in) return;
    if (!fresh) { notices.refresh_failed(PrinterProvider::flashforge); return; }
    // Present each current device immediately so its own name is used, while
    // the shared notice model retains per-device announcement deduplication.
    for (const auto& device : devices) {
        FirmwareUpdate update;
        update.device_id = device.id;
        update.availability = device.firmware_update_available ? FirmwareAvailability::available : FirmwareAvailability::unknown;
        update.title = device.firmware_update_title;
        update.tips = device.firmware_update_tips;
        notices.replace(PrinterProvider::flashforge, {update}, FirmwareNotices::Clock::now(), std::chrono::minutes(5));
        announce(PrinterProvider::flashforge, device.name);
    }
}
}}
