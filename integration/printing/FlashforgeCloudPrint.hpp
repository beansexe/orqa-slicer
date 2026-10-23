#pragma once

#include "../FlashNetwork.h"
#include <atomic>
#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <vector>

namespace Slic3r::VendorPrinting {

struct CloudPrinter {
    std::string id; // flashforge:<cloud devId>; never a display name or serial.
    std::string serial_number;
    std::string topic;
    std::string model_id;
    double nozzle_diameter = 0.0;
    std::string status; // A fresh status snapshot supplied by the account worker.
};

struct MaterialMapping {
    int tool_id = 0;
    int slot_id = 0;
    std::string material_name;
    std::string tool_color;
    std::string slot_color;
};

struct CloudPrintRequest {
    CloudPrinter printer;
    std::string job_path; // Absolute UTF-8 path to sliced G-code 3MF.
    std::string thumbnail_path; // Absolute UTF-8 PNG path.
    std::string destination_name; // A filename, including .3mf.
    int plate_number = 0; // Zero-based vendor plateNo; archive filenames are one-based.
    std::string union_id;
    bool print_now = true;
    bool leveling = false;
    bool flow_calibration = false;
    bool first_layer_inspection = false;
    bool time_lapse = false;
    bool use_material_station = false;
    std::vector<MaterialMapping> materials;
};

struct ValidatedCloudFiles {
    std::uint64_t job_size = 0;
    std::uint64_t thumbnail_size = 0;
    std::string job_md5;
    std::string thumbnail_md5;
    std::string model_id;
    double nozzle_diameter = 0.0;
    int plate_number = -1;
};

// The slicer bridge must inspect archive contents and selected plate metadata,
// not only ZIP magic. Reuse the slicer's existing ZIP reader and MD5 helper.
using CloudFileInspector = std::function<std::optional<ValidatedCloudFiles>(const CloudPrintRequest&)>;
using CloudProgress = std::function<void(long long, long long)>;

struct FlashforgeCloudApi {
    decltype(&fnet_wanDevSendGcodeClound) upload = nullptr;
    decltype(&fnet_freeCloundGcodeData) free_upload = nullptr;
    decltype(&fnet_wanDevAddCloundJob) create_job = nullptr;
    decltype(&fnet_freeAddCloudJobResults) free_jobs = nullptr;
    decltype(&fnet_connectionSendMulti) send = nullptr;
    decltype(&fnet_freeWriteMultiResult) free_send = nullptr;
    bool complete() const;
};

// This view is valid only for one synchronous worker operation. Do not persist
// it, log its fields, expose it to the GUI, or let it outlive the account worker.
struct CloudSessionView {
    const std::string& client_id;
    const std::string& access_token;
    void* connection = nullptr;
    bool authenticated = false;
};

enum class CloudPrintOutcome {
    rejected,
    cancelled,
    upload_failed,
    created_not_started,
    needs_operator_review,
    delivered // MQTT accepted the request; printer execution is not confirmed.
};

struct CloudPrintResult {
    CloudPrintOutcome outcome = CloudPrintOutcome::rejected;
    std::string message;
    std::string job_id;
    int vendor_code = FNET_ERROR;
};

// No automatic retries: job creation and MQTT publish may have succeeded even
// when their replies were lost. An operator must check the printer before retry.
// Separate from Orca's existing Flashforge LAN PrintHost implementation.
class FlashforgeCloudPrint {
public:
    FlashforgeCloudPrint(FlashforgeCloudApi api, CloudFileInspector inspector);
    CloudPrintResult run(const CloudPrintRequest& request, const CloudSessionView& session,
                        const std::atomic<bool>& cancelled, CloudProgress progress = {});

private:
    FlashforgeCloudApi m_api;
    CloudFileInspector m_inspector;
};

} // namespace Slic3r::VendorPrinting
