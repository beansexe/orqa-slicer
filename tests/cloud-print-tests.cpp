#include "printing/FlashforgeCloudPrint.hpp"
#include "FlashNetwork.h"
#include <atomic>
#include <cstdint>
#include <exception>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iostream>
#include <ios>
#include <optional>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace {
using namespace Slic3r::VendorPrinting;
namespace fs = std::filesystem;

void require(bool condition, const char* message)
{
    if (!condition) throw std::runtime_error(message);
}

struct Mock {
    std::vector<std::string> calls;
    int upload_code = FNET_OK;
    int create_code = FNET_OK;
    int send_code = FNET_OK;
    int failed_count = 0;
    int upload_freed = 0;
    int jobs_freed = 0;
    int send_freed = 0;
    int inspections = 0;
    bool incomplete_upload = false;
    bool wrong_device = false;
    bool omit_job_id = false;
    bool invalid_job_count = false;
    bool mutate_files = false;
    bool invalid_archive = false;
    bool incompatible_model = false;
    bool bad_md5 = false;
    bool wrong_size = false;
    bool cancel_after_upload = false;
    int inspector_plate = 2;
    bool cancel_in_upload = false;
    bool cancel_in_create = false;
    bool cancel_in_send = false;
    bool print_now = true;
    fnet_add_clound_job_error_t job_error = FNET_ADD_CLOUND_JOB_OK;
    std::atomic<bool>* cancelled = nullptr;
};
Mock* mock = nullptr;
char bucket[] = "bucket";
char endpoint[] = "endpoint";
char key[] = "storage-key";
char url[] = "https://invalid.example/object";
char device[] = "dev-1";
char wrong_device[] = "dev-2";
char job_id[] = "job-1";
char empty[] = "";

int upload(const char* client, const char* token, const fnet_send_gcode_data_t* data,
           fnet_clound_gcode_data_t** output, int timeout)
{
    mock->calls.push_back("upload");
    require(std::string(client) == "client-id" && std::string(token) == "private-test-token", "wrong session");
    require(timeout > 0 && data->gcodeToolCnt == 1, "invalid upload controls");
    require(data->printNow == static_cast<int>(mock->print_now) && data->levelingBeforePrint == 1, "lost upload settings");
    require(std::string(data->materialMappings[0].materialName) == "PLA", "lost material settings");
    *output = new fnet_clound_gcode_data_t{bucket, endpoint, key, mock->incomplete_upload ? empty : url, key, url};
    if (mock->cancel_in_upload) mock->cancelled->store(true);
    const int callback_code = data->callback(1, 2, data->callbackData);
    if (callback_code != 0) return FNET_ABORTED_BY_CALLBACK;
    return mock->upload_code;
}

void free_upload(fnet_clound_gcode_data_t* value)
{
    ++mock->upload_freed;
    delete value;
}

int create_job(const char* client, const char* token, const fnet_clound_job_data_t* data,
               fnet_add_clound_job_result_t** output, int* count, int timeout)
{
    mock->calls.push_back("create");
    require(std::string(client) == "client-id" && std::string(token) == "private-test-token", "wrong create session");
    require(timeout > 0 && data->devCnt == 1 && std::string(data->devIds[0]) == "dev-1", "wrong cloud identity");
    require(data->devSerialNumbers == nullptr && data->jobIds == nullptr, "premature start identifiers");
    require(std::string(data->gcodeType) == "3mf" && std::string(data->thumbType) == "png", "wrong file types");
    require(std::string(data->thumbName) == "test-part.3mf" && data->plateNo == mock->inspector_plate, "wrong thumbnail or plate");
    require(data->printNow == static_cast<int>(mock->print_now) && data->levelingBeforePrint == 1 && data->gcodeToolCnt == 1, "lost job controls");
    require(data->gcodeSize == 9 && data->thumbSize == 12, "wrong file sizes");
    require(std::string(data->unionId) == "union-id", "wrong union id");
    *count = mock->invalid_job_count ? 2 : 1;
    *output = new fnet_add_clound_job_result_t[*count]{};
    (*output)[0] = {mock->job_error, mock->wrong_device ? wrong_device : device, mock->omit_job_id ? empty : job_id};
    if (mock->cancel_in_create) mock->cancelled->store(true);
    return mock->create_code;
}

void free_jobs(fnet_add_clound_job_result_t* value, int)
{
    ++mock->jobs_freed;
    delete[] value;
}

int send(void* connection, const fnet_conn_write_multi_data_t* data, fnet_conn_write_multi_result_t** output)
{
    mock->calls.push_back("send");
    require(connection == mock && data->type == FNET_CONN_WRITE_START_CLOUND_JOB && data->qos == 1, "wrong MQTT transport");
    require(data->dataCnt == 1 && data->topicCnt == 1 && std::string(data->topics[0]) == "device-topic", "wrong topic");
    const auto& job = *static_cast<const fnet_clound_job_data_t*>(data->datas[0]);
    require(std::string(job.jobIds[0]) == "job-1" && std::string(job.devSerialNumbers[0]) == "serial-1", "missing MQTT job identity");
    require(std::string(job.devIds[0]) == "dev-1" && job.printNow == static_cast<int>(mock->print_now), "lost MQTT controls");
    *output = new fnet_conn_write_multi_result_t{nullptr, mock->failed_count};
    if (mock->cancel_in_send) mock->cancelled->store(true);
    return mock->send_code;
}

void free_send(const fnet_conn_write_multi_result_t* value)
{
    ++mock->send_freed;
    delete value;
}

FlashforgeCloudApi api() { return {upload, free_upload, create_job, free_jobs, send, free_send}; }

CloudFileInspector inspector()
{
    return [](const CloudPrintRequest& request) -> std::optional<ValidatedCloudFiles> {
        ++mock->inspections;
        if (mock->invalid_archive) return std::nullopt;
        if (mock->cancel_after_upload && mock->inspections == 2) mock->cancelled->store(true);
        return ValidatedCloudFiles{fs::file_size(fs::u8path(request.job_path)) + static_cast<unsigned int>(mock->wrong_size), fs::file_size(fs::u8path(request.thumbnail_path)),
            mock->bad_md5 ? "not-md5" : mock->mutate_files && mock->inspections == 2 ? "0123456789abcdef0123456789abcdef" : "123456789abcdef0123456789abcdef0",
            "abcdef0123456789abcdef0123456789",
            mock->incompatible_model ? "Flashforge-Adventurer-5M-Pro" : "Flashforge-Adventurer-5M", 0.4, mock->inspector_plate};
    };
}

CloudPrintRequest request(const fs::path& fixtures)
{
    CloudPrintRequest value;
    value.printer = {"flashforge:dev-1", "serial-1", "device-topic", "Flashforge-Adventurer-5M", 0.4, "ready"};
    value.job_path = (fixtures / "part.3mf").u8string();
    value.thumbnail_path = (fixtures / "part.png").u8string();
    value.destination_name = "test-part.3mf";
    value.plate_number = 2;
    value.union_id = "union-id";
    value.leveling = true;
    value.materials.push_back({0, 0, "PLA", "#FFFFFF", "#FFFFFF"});
    return value;
}

void write_fixtures(const fs::path& fixtures)
{
    fs::create_directories(fixtures);
    // These are signature fixtures, not print-ready archives. Archive inspection
    // is mocked; production must use the slicer's archive/plate metadata reader.
    const unsigned char job[] = {0x50, 0x4b, 0x03, 0x04, 1, 2, 3, 4, 5};
    const unsigned char thumbnail[] = {0x89, 0x50, 0x4e, 0x47, 0x0d, 0x0a, 0x1a, 0x0a, 1, 2, 3, 4};
    std::ofstream job_file(fixtures / "part.3mf", std::ios::binary);
    job_file.write(reinterpret_cast<const char*>(job), sizeof(job));
    std::ofstream thumbnail_file(fixtures / "part.png", std::ios::binary);
    thumbnail_file.write(reinterpret_cast<const char*>(thumbnail), sizeof(thumbnail));
}

void check_case(const fs::path& fixtures, const std::function<void(Mock&, CloudPrintRequest&)>& configure,
                CloudPrintOutcome expected, std::vector<std::string> calls)
{
    write_fixtures(fixtures);
    Mock state;
    std::atomic<bool> cancelled{false};
    state.cancelled = &cancelled;
    mock = &state;
    auto data = request(fixtures);
    configure(state, data);
    state.print_now = data.print_now;
    FlashforgeCloudPrint workflow(api(), inspector());
    const std::string client("client-id"), secret("private-test-token");
    const auto output = workflow.run(data, {client, secret, &state, true}, cancelled);
    require(output.outcome == expected, "wrong outcome");
    require(state.calls == calls, "wrong operation sequence or automatic retry");
    require(output.message.find(secret) == std::string::npos, "credential leaked into result");
    require(state.upload_freed == (calls.empty() ? 0 : 1), "upload response not freed");
    require(state.jobs_freed == (calls.size() >= 2 ? 1 : 0), "job response not freed");
    require(state.send_freed == (calls.size() >= 3 ? 1 : 0), "MQTT response not freed");
    if (expected == CloudPrintOutcome::delivered || expected == CloudPrintOutcome::created_not_started || calls.size() == 3)
        require(output.job_id == "job-1", "operator lost cloud job identifier");
    if (state.create_code != FNET_OK && calls.size() >= 2)
        require(output.job_id == "job-1", "known job identifier lost after uncertain creation");
}
}

int main()
{
    try {
        const auto fixtures = fs::absolute(fs::path("artifacts") / "cloud-print-fixtures");
        write_fixtures(fixtures);
        const std::vector<std::string> all{"upload", "create", "send"};
        check_case(fixtures, [](auto&, auto&) {}, CloudPrintOutcome::delivered, all);
        check_case(fixtures, [](auto&, auto& r) { r.print_now = false; }, CloudPrintOutcome::delivered, all);
        check_case(fixtures, [](auto& m, auto&) { m.cancelled->store(true); }, CloudPrintOutcome::cancelled, {});
        check_case(fixtures, [](auto&, auto& r) { r.printer.id = "bambu:dev-1"; }, CloudPrintOutcome::rejected, {});
        check_case(fixtures, [](auto&, auto& r) { r.printer.status = "printing"; }, CloudPrintOutcome::rejected, {});
        check_case(fixtures, [](auto&, auto& r) { r.destination_name = "../part.3mf"; }, CloudPrintOutcome::rejected, {});
        check_case(fixtures, [](auto&, auto& r) { r.use_material_station = true; }, CloudPrintOutcome::rejected, {});
        check_case(fixtures, [](auto&, auto& r) { r.printer.nozzle_diameter = 0.6; }, CloudPrintOutcome::rejected, {});
        check_case(fixtures, [](auto& m, auto&) { m.invalid_archive = true; }, CloudPrintOutcome::rejected, {});
        check_case(fixtures, [](auto& m, auto&) { m.incompatible_model = true; }, CloudPrintOutcome::rejected, {});
        check_case(fixtures, [](auto& m, auto&) { m.bad_md5 = true; }, CloudPrintOutcome::rejected, {});
        check_case(fixtures, [](auto& m, auto&) { m.wrong_size = true; }, CloudPrintOutcome::rejected, {});
        check_case(fixtures, [](auto&, auto& r) { r.plate_number = -1; }, CloudPrintOutcome::rejected, {});
        check_case(fixtures, [](auto& m, auto& r) { m.inspector_plate = 0; r.plate_number = 0; }, CloudPrintOutcome::delivered, all);
        check_case(fixtures, [](auto& m, auto&) { m.inspector_plate = 3; }, CloudPrintOutcome::rejected, {});
        check_case(fixtures, [](auto&, auto& r) { r.materials.push_back(r.materials[0]); }, CloudPrintOutcome::rejected, {});
        check_case(fixtures, [](auto&, auto& r) { r.printer.topic.clear(); }, CloudPrintOutcome::rejected, {});
        check_case(fixtures, [](auto&, auto& r) { r.job_path = "relative.3mf"; }, CloudPrintOutcome::rejected, {});
        check_case(fixtures, [](auto&, auto& r) {
            std::fstream file(fs::u8path(r.thumbnail_path), std::ios::in | std::ios::out | std::ios::binary);
            file.put('x');
        }, CloudPrintOutcome::rejected, {});
        check_case(fixtures, [](auto& m, auto&) { m.upload_code = FNET_ERROR; }, CloudPrintOutcome::upload_failed, {"upload"});
        check_case(fixtures, [](auto& m, auto&) { m.incomplete_upload = true; }, CloudPrintOutcome::upload_failed, {"upload"});
        check_case(fixtures, [](auto& m, auto&) { m.mutate_files = true; }, CloudPrintOutcome::upload_failed, {"upload"});
        check_case(fixtures, [](auto& m, auto&) { m.cancel_in_upload = true; }, CloudPrintOutcome::cancelled, {"upload"});
        check_case(fixtures, [](auto& m, auto&) { m.cancel_after_upload = true; }, CloudPrintOutcome::cancelled, {"upload"});
        check_case(fixtures, [](auto& m, auto&) { m.create_code = FNET_ERROR; }, CloudPrintOutcome::needs_operator_review, {"upload", "create"});
        check_case(fixtures, [](auto& m, auto&) { m.wrong_device = true; }, CloudPrintOutcome::needs_operator_review, {"upload", "create"});
        check_case(fixtures, [](auto& m, auto&) { m.omit_job_id = true; }, CloudPrintOutcome::needs_operator_review, {"upload", "create"});
        check_case(fixtures, [](auto& m, auto&) { m.invalid_job_count = true; }, CloudPrintOutcome::needs_operator_review, {"upload", "create"});
        check_case(fixtures, [](auto& m, auto&) { m.job_error = FNET_ADD_CLOUND_JOB_DEVICE_BUSY; }, CloudPrintOutcome::rejected, {"upload", "create"});
        check_case(fixtures, [](auto& m, auto&) { m.cancel_in_create = true; }, CloudPrintOutcome::created_not_started, {"upload", "create"});
        check_case(fixtures, [](auto& m, auto&) { m.send_code = FNET_CONN_SEND_ERROR; }, CloudPrintOutcome::needs_operator_review, all);
        check_case(fixtures, [](auto& m, auto&) { m.failed_count = 1; }, CloudPrintOutcome::needs_operator_review, all);
        check_case(fixtures, [](auto& m, auto&) { m.cancel_in_send = true; }, CloudPrintOutcome::needs_operator_review, all);
        Mock state;
        std::atomic<bool> cancelled{false};
        state.cancelled = &cancelled;
        mock = &state;
        const std::string client("client-id"), secret("private-test-token");
        FlashforgeCloudPrint workflow(api(), inspector());
        auto output = workflow.run(request(fixtures), {client, secret, &state, false}, cancelled);
        require(output.outcome == CloudPrintOutcome::rejected && state.calls.empty(), "unauthenticated operation attempted");
        output = workflow.run(request(fixtures), {client, secret, &state, true}, cancelled,
                              [](long long, long long) { throw std::runtime_error("callback failure"); });
        require(output.outcome == CloudPrintOutcome::upload_failed && state.calls == std::vector<std::string>{"upload"}, "callback exception crossed ABI");
        std::cout << "35 simulated cloud print cases passed; no network or printer calls.\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "Cloud workflow test failed: " << error.what() << '\n';
        return 1;
    }
}
