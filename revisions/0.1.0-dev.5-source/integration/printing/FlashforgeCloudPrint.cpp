#include "FlashforgeCloudPrint.hpp"
#include "../FlashNetwork.h"
#include <array>
#include <atomic>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <functional>
#include <ios>
#include <istream>
#include <limits>
#include <memory>
#include <optional>
#include <string>
#include <system_error>
#include <utility>
#include <vector>

namespace Slic3r::VendorPrinting {
namespace {
constexpr int timeout_ms = 30000;
constexpr const char* id_prefix = "flashforge:";

CloudPrintResult result(CloudPrintOutcome outcome, std::string message, int code = FNET_ERROR,
                        std::string job_id = {})
{
    return {outcome, std::move(message), std::move(job_id), code};
}

bool contains_control(const std::string& value)
{
    for (unsigned char c : value) if (c < 32 || c == 127) return true;
    return false;
}

bool md5_valid(const std::string& value)
{
    if (value.size() != 32) return false;
    for (char c : value)
        if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F'))) return false;
    return true;
}

bool same_nozzle(double a, double b)
{
    return std::isfinite(a) && std::isfinite(b) && a > 0.0 && b > 0.0 && std::abs(a - b) < 0.0001;
}

bool file_has_signature(const std::filesystem::path& path, const unsigned char* signature,
                        std::size_t signature_size, std::uint64_t expected_size)
{
    std::error_code error;
    if (!path.is_absolute() || !std::filesystem::is_regular_file(path, error) || error) return false;
    const auto size = std::filesystem::file_size(path, error);
    if (error || size != expected_size || size < signature_size || size > static_cast<std::uint64_t>(std::numeric_limits<long long>::max())) return false;
    std::ifstream file(path, std::ios::binary);
    std::array<char, 8> header{};
    file.read(header.data(), static_cast<std::streamsize>(signature_size));
    if (!file) return false;
    for (std::size_t i = 0; i < signature_size; ++i)
        if (static_cast<unsigned char>(header[i]) != signature[i]) return false;
    return true;
}

bool files_valid(const CloudPrintRequest& request, const ValidatedCloudFiles& files)
{
    const auto job_path = std::filesystem::u8path(request.job_path);
    const auto thumbnail_path = std::filesystem::u8path(request.thumbnail_path);
    constexpr unsigned char zip_magic[] = {0x50, 0x4b, 0x03, 0x04};
    constexpr unsigned char png_magic[] = {0x89, 0x50, 0x4e, 0x47, 0x0d, 0x0a, 0x1a, 0x0a};
    return job_path.extension() == ".3mf" && thumbnail_path.extension() == ".png" &&
        md5_valid(files.job_md5) && md5_valid(files.thumbnail_md5) && files.model_id == request.printer.model_id &&
        same_nozzle(files.nozzle_diameter, request.printer.nozzle_diameter) && files.plate_number == request.plate_number &&
        file_has_signature(job_path, zip_magic, sizeof(zip_magic), files.job_size) &&
        file_has_signature(thumbnail_path, png_magic, sizeof(png_magic), files.thumbnail_size);
}

bool request_valid(const CloudPrintRequest& request)
{
    const std::string prefix(id_prefix);
    const auto& printer = request.printer;
    if (printer.id.compare(0, prefix.size(), prefix) != 0 || printer.id.size() == prefix.size() ||
        contains_control(printer.id) || printer.serial_number.empty() || contains_control(printer.serial_number) ||
        printer.topic.empty() || contains_control(printer.topic) || printer.model_id.empty() ||
        printer.status != "ready" || request.plate_number < 0 ||
        request.destination_name.size() > 255 || request.destination_name.size() <= 4 ||
        contains_control(request.destination_name) || request.destination_name.find_first_of("/\\:") != std::string::npos ||
        std::filesystem::u8path(request.destination_name).extension() != ".3mf" ||
        request.materials.empty() || request.materials.size() > static_cast<std::size_t>(std::numeric_limits<int>::max())) return false;
    for (std::size_t i = 0; i < request.materials.size(); ++i) {
        const auto& material = request.materials[i];
        if (material.tool_id < 0 || material.slot_id < 0 || material.material_name.empty()) return false;
        for (std::size_t j = 0; j < i; ++j) if (request.materials[j].tool_id == material.tool_id) return false;
    }
    // A5M has no material station, flow-calibration or first-layer inspection.
    // Further capabilities must come from the selected model's product detail.
    if (printer.model_id == "Flashforge-Adventurer-5M" || printer.model_id == "Flashforge-Adventurer-5M-Pro")
        return !request.use_material_station && !request.flow_calibration && !request.first_layer_inspection;
    return true;
}

struct ProgressContext {
    const std::atomic<bool>& cancelled;
    const CloudProgress& progress;
    bool callback_failed = false;
};

int progress_callback(long long now, long long total, void* data)
{
    auto& context = *static_cast<ProgressContext*>(data);
    if (context.cancelled.load()) return 1;
    try {
        if (context.progress) context.progress(now, total);
    } catch (...) {
        context.callback_failed = true;
        return 1; // Never allow exceptions to cross the vendor C ABI.
    }
    return context.cancelled.load() ? 1 : 0;
}

bool same_files(const ValidatedCloudFiles& a, const ValidatedCloudFiles& b)
{
    return a.job_size == b.job_size && a.thumbnail_size == b.thumbnail_size &&
        a.job_md5 == b.job_md5 && a.thumbnail_md5 == b.thumbnail_md5 &&
        a.model_id == b.model_id && same_nozzle(a.nozzle_diameter, b.nozzle_diameter) &&
        a.plate_number == b.plate_number;
}

bool nonempty(const char* value) { return value != nullptr && *value != '\0'; }
}

bool FlashforgeCloudApi::complete() const
{
    return upload && free_upload && create_job && free_jobs && send && free_send;
}

FlashforgeCloudPrint::FlashforgeCloudPrint(FlashforgeCloudApi api, CloudFileInspector inspector)
    : m_api(api), m_inspector(std::move(inspector))
{}

CloudPrintResult FlashforgeCloudPrint::run(const CloudPrintRequest& request, const CloudSessionView& session,
                                         const std::atomic<bool>& cancelled, CloudProgress progress)
{
    if (cancelled.load()) return result(CloudPrintOutcome::cancelled, "Cancelled before upload.", FNET_ABORTED_BY_CALLBACK);
    if (!m_api.complete() || !m_inspector || !session.authenticated || session.client_id.empty() ||
        session.access_token.empty() || session.connection == nullptr)
        return result(CloudPrintOutcome::rejected, "Flashforge cloud printing requires an authenticated account and connection.");
    std::optional<ValidatedCloudFiles> files;
    try {
        if (!request_valid(request)) return result(CloudPrintOutcome::rejected, "Check the printer, compatible profile, plate and print options.");
        files = m_inspector(request);
        if (!files || !files_valid(request, *files))
            return result(CloudPrintOutcome::rejected, "A compatible sliced 3MF and valid PNG thumbnail are required.");
    } catch (...) {
        return result(CloudPrintOutcome::rejected, "The selected print files could not be validated.");
    }
    if (cancelled.load()) return result(CloudPrintOutcome::cancelled, "Cancelled before upload.", FNET_ABORTED_BY_CALLBACK);

    std::vector<fnet_material_mapping_t> materials;
    materials.reserve(request.materials.size());
    for (const auto& material : request.materials)
        materials.push_back({material.tool_id, material.slot_id, material.material_name.c_str(),
                             material.tool_color.c_str(), material.slot_color.c_str()});
    ProgressContext progress_context{cancelled, progress};
    fnet_send_gcode_data_t upload{};
    upload.gcodeFilePath = request.job_path.c_str();
    upload.thumbFilePath = request.thumbnail_path.c_str();
    upload.gcodeDstName = request.destination_name.c_str();
    upload.printNow = request.print_now;
    upload.levelingBeforePrint = request.leveling;
    upload.flowCalibration = request.flow_calibration;
    upload.firstLayerInspection = request.first_layer_inspection;
    upload.timeLapseVideo = request.time_lapse;
    upload.useMatlStation = request.use_material_station;
    upload.gcodeToolCnt = static_cast<int>(materials.size());
    upload.materialMappings = materials.data();
    upload.callback = progress_callback;
    upload.callbackData = &progress_context;
    fnet_clound_gcode_data_t* raw_upload = nullptr;
    const int upload_code = m_api.upload(session.client_id.c_str(), session.access_token.c_str(), &upload, &raw_upload, timeout_ms);
    std::unique_ptr<fnet_clound_gcode_data_t, decltype(m_api.free_upload)> uploaded(raw_upload, m_api.free_upload);
    if (progress_context.callback_failed)
        return result(CloudPrintOutcome::upload_failed, "Upload progress could not be handled; no print job was created.", upload_code);
    if (cancelled.load() || upload_code == FNET_ABORTED_BY_CALLBACK)
        return result(CloudPrintOutcome::cancelled, "Upload cancelled; no print job was created.", upload_code);
    if (upload_code != FNET_OK)
        return result(CloudPrintOutcome::upload_failed, "Upload failed; no print job was created.", upload_code);
    if (!uploaded || !nonempty(uploaded->bucketName) || !nonempty(uploaded->endpoint) ||
        !nonempty(uploaded->gcodeStorageKey) || !nonempty(uploaded->gcodeStorageUrl) ||
        !nonempty(uploaded->thumbStorageKey) || !nonempty(uploaded->thumbStorageUrl))
        return result(CloudPrintOutcome::upload_failed, "The cloud returned incomplete upload details.");
    try {
        const auto after_upload = m_inspector(request);
        if (!after_upload || !files_valid(request, *after_upload) || !same_files(*files, *after_upload))
            return result(CloudPrintOutcome::upload_failed, "The print files changed during upload; no print job was created.");
    } catch (...) {
        return result(CloudPrintOutcome::upload_failed, "The uploaded files could not be verified; no print job was created.");
    }
    if (cancelled.load()) return result(CloudPrintOutcome::cancelled, "Cancelled after upload; no print job was created.", FNET_ABORTED_BY_CALLBACK);

    const std::string raw_id = request.printer.id.substr(std::string(id_prefix).size());
    const char* dev_id = raw_id.c_str();
    const char* serial = request.printer.serial_number.c_str();
    const char* topic = request.printer.topic.c_str();
    fnet_clound_job_data_t job{};
    job.devIds = &dev_id;
    job.devCnt = 1;
    job.gcodeName = request.destination_name.c_str();
    job.gcodeType = "3mf";
    job.gcodeMd5 = files->job_md5.c_str();
    job.gcodeSize = static_cast<long long>(files->job_size);
    // The pinned vendor registers thumbName with the 3MF destination filename,
    // despite thumbType being png. Preserve its wire convention until verified.
    job.thumbName = request.destination_name.c_str();
    job.thumbType = "png";
    job.thumbMd5 = files->thumbnail_md5.c_str();
    job.thumbSize = static_cast<long long>(files->thumbnail_size);
    job.bucketName = uploaded->bucketName;
    job.endpoint = uploaded->endpoint;
    job.gcodeStorageKey = uploaded->gcodeStorageKey;
    job.gcodeStorageUrl = uploaded->gcodeStorageUrl;
    job.thumbStorageKey = uploaded->thumbStorageKey;
    job.thumbStorageUrl = uploaded->thumbStorageUrl;
    job.unionId = request.union_id.c_str();
    job.plateNo = request.plate_number;
    job.printNow = upload.printNow;
    job.levelingBeforePrint = upload.levelingBeforePrint;
    job.flowCalibration = upload.flowCalibration;
    job.firstLayerInspection = upload.firstLayerInspection;
    job.timeLapseVideo = upload.timeLapseVideo;
    job.useMatlStation = upload.useMatlStation;
    job.gcodeToolCnt = upload.gcodeToolCnt;
    job.materialMappings = upload.materialMappings;
    fnet_add_clound_job_result_t* raw_jobs = nullptr;
    int job_count = 0;
    const int create_code = m_api.create_job(session.client_id.c_str(), session.access_token.c_str(), &job, &raw_jobs, &job_count, timeout_ms);
    const auto free_jobs = [this, job_count](fnet_add_clound_job_result_t* value) { m_api.free_jobs(value, job_count); };
    std::unique_ptr<fnet_add_clound_job_result_t, decltype(free_jobs)> jobs(raw_jobs, free_jobs);
    const bool correct_device = jobs && job_count == 1 && nonempty(jobs->devId) && raw_id == jobs->devId;
    const std::string known_job_id = correct_device && nonempty(jobs->jobId) ? jobs->jobId : "";
    if (create_code != FNET_OK || !correct_device)
        return result(CloudPrintOutcome::needs_operator_review, "The cloud job could not be confirmed. Check the printer before retrying.", create_code, known_job_id);
    if (jobs->error != FNET_ADD_CLOUND_JOB_OK)
        return result(CloudPrintOutcome::rejected, "The cloud rejected this printer's job. Refresh its status before retrying.", static_cast<int>(jobs->error));
    if (!nonempty(jobs->jobId))
        return result(CloudPrintOutcome::needs_operator_review, "The cloud omitted the job identifier. Check the printer before retrying.");
    const std::string job_id(known_job_id);
    if (cancelled.load())
        return result(CloudPrintOutcome::created_not_started, "A cloud job was created; its print request was not sent. Check the printer before retrying.", FNET_ABORTED_BY_CALLBACK, job_id);

    const char* raw_job_id = job_id.c_str();
    job.devSerialNumbers = &serial;
    job.jobIds = &raw_job_id;
    const void* job_ptr = &job;
    fnet_conn_write_multi_data_t start{};
    start.type = FNET_CONN_WRITE_START_CLOUND_JOB;
    start.datas = &job_ptr;
    start.topics = &topic;
    start.dataCnt = 1;
    start.topicCnt = 1;
    start.qos = 1;
    fnet_conn_write_multi_result_t* raw_send = nullptr;
    const int send_code = m_api.send(session.connection, &start, &raw_send);
    std::unique_ptr<fnet_conn_write_multi_result_t, decltype(m_api.free_send)> sent(raw_send, m_api.free_send);
    if (send_code != FNET_OK || !sent || sent->failedCnt != 0 || cancelled.load())
        return result(CloudPrintOutcome::needs_operator_review, "The delivery result is uncertain. Check the printer before retrying.", send_code, job_id);
    return result(CloudPrintOutcome::delivered,
                  request.print_now ? "Print request delivered. Confirm that the printer starts." : "File delivery request sent. Confirm it on the printer.",
                  FNET_OK, job_id);
}

} // namespace Slic3r::VendorPrinting
