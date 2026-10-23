#include "FlashforgeAccount.hpp"
#include "FlashNetwork.h"
#include "printers/PrinterCatalog.hpp"
#include "printing/FlashforgeCloudConnection.hpp"
#include "printing/FlashforgeCloudPrint.hpp"
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <memory>
#include <optional>
#include <string>
#include <utility>

namespace Slic3r {
namespace {
std::string value(const char* text) { return text ? text : ""; }
std::optional<double> number(double input, double minimum, double maximum)
{
    return std::isfinite(input) && input >= minimum && input <= maximum
        ? std::optional<double>(input) : std::nullopt;
}
}

bool FlashforgeAccount::refresh_status(const std::string& id) { return fetch_status(id, true); }

bool FlashforgeAccount::fetch_status(const std::string& id, bool retry_refresh)
{
    m_live_status.reset();
    if (m_state != State::signed_in) return false;
    if (!m_api.detail || !m_api.free_product || !m_api.free_detail) {
        m_message = "The Flashforge plugin does not provide device status.";
        return false;
    }
    if (std::chrono::steady_clock::now() >= m_expiry) {
        const std::string secret = m_refresh_token;
        if (!retry_refresh || !restore(secret)) return false;
        return fetch_status(id, false);
    }
    if (std::none_of(m_devices.begin(), m_devices.end(), [&id](const FlashforgeDevice& item) { return item.id == id; })) {
        m_message = "This printer is no longer in the current Flashforge account. Refresh the printer list.";
        return false;
    }
    const std::string native = id.substr(std::string("flashforge:").size());
    fnet_dev_product_t* raw_product = nullptr;
    fnet_dev_detail_t* raw_detail = nullptr;
    const int result = m_api.detail(m_client_id.c_str(), m_access_token.c_str(), native.c_str(),
                                   &raw_product, &raw_detail, 15000);
    const std::unique_ptr<fnet_dev_product_t, decltype(m_api.free_product)> product(raw_product, m_api.free_product);
    const std::unique_ptr<fnet_dev_detail_t, decltype(m_api.free_detail)> detail(raw_detail, m_api.free_detail);
    if (result == FNET_UNAUTHORIZED) {
        if (retry_refresh) {
            const std::string secret = m_refresh_token;
            if (restore(secret)) return fetch_status(id, false);
        }
        clear_session();
        m_message = "Flashforge sign-in expired. Please sign in again.";
        return false;
    }
    if (result != FNET_OK || !detail || value(detail->devId) != native ||
        detail->nozzleCnt < 0 || detail->nozzleCnt > 16) {
        m_message = "The printer's cloud status could not be refreshed. Check the printer connection and try again.";
        return false;
    }
    FlashforgeLiveStatus status;
    status.id = id;
    status.status = value(detail->status);
    status.firmware = value(detail->firmwareVersion);
    status.print_file = value(detail->printFileName);
    const double nozzle = detail->nozzleTemps && detail->nozzleCnt > 0 ? detail->nozzleTemps[0] : detail->rightTemp;
    const double target = detail->nozzleTargetTemps && detail->nozzleCnt > 0 ? detail->nozzleTargetTemps[0] : detail->rightTargetTemp;
    status.nozzle_temperature = number(nozzle, -50., 1000.);
    status.nozzle_target = number(target, 0., 1000.);
    status.bed_temperature = number(detail->platTemp, -50., 300.);
    status.bed_target = number(detail->platTargetTemp, 0., 300.);
    status.progress = number(detail->printProgress, 0., 1.);
    if (detail->printLayer >= 0 && detail->targetPrintLayer >= detail->printLayer) {
        status.layer = detail->printLayer;
        status.total_layers = detail->targetPrintLayer;
    }
    m_live_status = std::move(status);
    m_message = "Flashforge device status refreshed through your cloud account.";
    return true;
}

VendorPrinting::CloudPrintResult FlashforgeAccount::send_cloud(VendorPrinting::CloudPrintRequest request,
    VendorPrinting::CloudFileInspector inspector, const std::atomic<bool>& cancelled)
{
    using namespace VendorPrinting;
    const auto rejected = [](const std::string& message) {
        return CloudPrintResult{CloudPrintOutcome::rejected, message, {}, FNET_ERROR};
    };
    if (!m_library || !m_cloud_api.complete() || !m_connection_api.complete())
        return rejected("This Flashforge plugin does not provide the cloud printing functions.");
    if (cancelled.load() || !refresh_devices() || !refresh_status(request.printer.id))
        return rejected("Cloud printing needs a fresh authenticated printer status. Refresh or sign in again.");
    const auto found = std::find_if(m_devices.begin(), m_devices.end(), [&request](const FlashforgeDevice& device) {
        return device.id == request.printer.id;
    });
    if (found == m_devices.end() || found->serial_number.empty() || found->topic.empty() || !m_live_status)
        return rejected("The Flashforge account returned incomplete printer routing information.");
    if (m_live_status->status != "ready") return rejected("This printer is not ready. Check its live status before sending.");
    if (canonical_printer_model(PrinterProvider::flashforge, found->model) !=
        canonical_printer_model(PrinterProvider::flashforge, request.printer.model_id))
        return rejected("The selected slicing profile does not match this Flashforge printer.");
    request.printer.serial_number = found->serial_number;
    request.printer.topic = found->topic;
    request.printer.status = m_live_status->status;
    const CloudConnectionSession session(m_client_id, m_access_token, m_expiry, m_account_epoch);
    // This one sequential operation owns the account. Callbacks receive an
    // immutable session copy, never the sequential account or UI object.
    FlashforgeCloudConnection connection(m_connection_api, m_library, [session] { return std::optional<CloudConnectionSession>(session); });
    if (!connection.open({found->topic}) || cancelled.load()) return rejected(connection.message());
    auto api = m_cloud_api;
    api.send = FlashforgeCloudConnection::send_checked;
    FlashforgeCloudPrint worker(api, std::move(inspector));
    const CloudSessionView view{m_client_id, m_access_token, connection.workflow_context(), m_state == State::signed_in};
    auto result = worker.run(request, view, cancelled);
    m_message = result.message;
    return result;
}
} // namespace Slic3r
