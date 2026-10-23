#include "SolidWorksBridge.hpp"
#include "../printing/FastStartWarning.hpp"
#include "../VendorAccountsDialog.hpp"
#include "../printers/PrinterCatalog.hpp"
#include "../printing/FlashforgeArchiveInspector.hpp"
#include "libslic3r/AppConfig.hpp"
#include "libslic3r/Config.hpp"
#include "libslic3r/Model.hpp"
#include "libslic3r/Preset.hpp"
#include "libslic3r/PresetBundle.hpp"
#include "libslic3r/Utils.hpp"
#include "libslic3r/Format/bbs_3mf.hpp"
#include "slic3r/GUI/GUI_App.hpp"
#include "slic3r/GUI/MainFrame.hpp"
#include "slic3r/GUI/Plater.hpp"
#include "slic3r/GUI/Monitor.hpp"
#include "slic3r/GUI/DeviceManager.hpp"
#include "slic3r/GUI/DeviceCore/DevConfigUtil.h"
#include "slic3r/GUI/DeviceCore/DevManager.h"
#include "slic3r/Utils/NetworkAgent.hpp"
#include "slic3r/Utils/CloudProvider.hpp"
#include <wx/timer.h>
#include <wx/window.h>
#include <wx/utils.h>
#include <wx/file.h>
#include <wx/stdpaths.h>
#include <nlohmann/json.hpp>
#include <boost/uuid/uuid_generators.hpp>
#include <boost/uuid/uuid_io.hpp>
#include <algorithm>
#include <atomic>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <map>
#include <sstream>
#include <memory>
#include <set>
#include <stdexcept>
#include <thread>

namespace Slic3r { namespace GUI { namespace {
namespace fs = std::filesystem;
using Json = nlohmann::json;
constexpr const char* current = "@current";
std::string text_option(const DynamicPrintConfig& c, const char* key) {
    auto* value = c.option<ConfigOptionString>(key);
    return value ? value->value : std::string();
}
std::string first_option(const DynamicPrintConfig& c, const char* key) {
    auto* value = c.option<ConfigOptionStrings>(key);
    return value && !value->values.empty() ? value->values.front() : std::string();
}
Json read_json(const fs::path& path) {
    if (fs::is_symlink(path) || fs::file_size(path) > 1024 * 1024) throw std::runtime_error("Invalid bridge request");
    std::ifstream stream(path, std::ios::binary);
    return Json::parse(stream);
}
void write_json(const fs::path& path, const Json& value) {
    wxTempFile file(wxString::FromUTF8(path.u8string()));
    if (!file.IsOpened() || !file.Write(wxString::FromUTF8(value.dump())) || !file.Commit())
        throw std::runtime_error("Cannot write SolidWorks bridge response");
}
void write_preset(const fs::path& path, const DynamicPrintConfig& config, const std::string& name,
                  const char* type, const std::string& filament_id = {}) {
    // All fields are already resolved by the live bundle. Flatten inheritance so
    // the isolated CLI never needs to read the user's account/profile directories.
    std::ostringstream stream;
    config.save_to_json(stream, name, "user", "2.3.0.0");
    auto json = Json::parse(stream.str());
    json["type"] = type; json["inherits"] = "";
    if (!filament_id.empty()) json["filament_id"] = filament_id;
    write_json(path, json);
}
bool request_id(const std::string& name) {
    if (name.size() != 32) return false;
    return std::all_of(name.begin(), name.end(), [](char c) { return (c >= 'a' && c <= 'f') || (c >= '0' && c <= '9'); });
}
struct Device { std::string id, name, model; PrinterProvider provider; bool ready = false; };
struct Settings {
    std::string device, profile, model_id;
    DynamicPrintConfig full, machine, process, filament;
    double nozzle = 0.;
};

class Bridge final : public wxEvtHandler {
    wxTimer timer;
    fs::path root;
    std::string session;
    std::map<std::string, Settings> prepared;
    std::thread worker;
    std::atomic<bool> worker_done{true}, stopping{false};
    std::atomic<int> worker_result{-1};
    fs::path pending;
    bool flash_pending = false, handling = false;
    std::set<std::string> consumed;
    std::map<std::string, std::vector<ObjectID>> imported;
    int ticks = 0;
public:
    explicit Bridge(wxWindow* owner) : timer(this) {
        wxString local;
        if (!wxGetEnv("LOCALAPPDATA", &local)) throw std::runtime_error("LOCALAPPDATA unavailable");
        session = boost::uuids::to_string(boost::uuids::random_generator()());
        root = fs::u8path(local.ToUTF8().data()) / "OrqaSolidWorks" / "bridge" / (std::to_string(wxGetProcessId()) + "-" + session);
        fs::create_directories(root / "requests");
        // A process-ID can be reused. Only this randomly generated session is accepted.
        heartbeat();
        Bind(wxEVT_TIMER, [this](wxTimerEvent&) { tick(); });
        owner->Bind(wxEVT_DESTROY, [owner](wxWindowDestroyEvent& e) {
            if (e.GetEventObject() == owner) stop_solidworks_bridge();
            e.Skip();
        });
        timer.Start(250);
    }
    ~Bridge() override {
        timer.Stop(); stopping = true;
        if (worker.joinable()) worker.join();
        std::error_code error;
        fs::remove_all(root, error); // Only this bridge's random, session-owned directory.
    }
private:
    void heartbeat() {
        write_json(root / "endpoint.json", {{"schema", 1}, {"session", session},
            {"pid", wxGetProcessId()}, {"updated", std::chrono::duration_cast<std::chrono::seconds>(
                std::chrono::system_clock::now().time_since_epoch()).count()},
            {"executable", wxStandardPaths::Get().GetExecutablePath().ToUTF8().data()}});
    }
    std::vector<Device> devices() {
        std::vector<Device> result;
        auto* manager = wxGetApp().getDeviceManager();
        if (manager && wxGetApp().is_user_login(BBL_CLOUD_PROVIDER)) {
            for (const auto& pair : manager->get_my_cloud_machine_list("bbl")) {
                auto* m = pair.second;
                if (!m || m->printer_agent_id != "bbl" || !m->is_online()) continue;
                auto model = DevPrinterConfigUtil::get_printer_display_name(m->get_show_printer_type());
                if (model.empty()) model = m->get_show_printer_type();
                result.push_back({qualified_printer_id(PrinterProvider::bambu, m->get_dev_id()),
                    m->get_dev_name(), model, PrinterProvider::bambu, m->can_print()});
            }
        }
        auto flash = flashforge_account_snapshot();
        if (flash.state == FlashforgeAccount::State::signed_in && !flash.refresh_failed) {
            for (const auto& d : flash.devices) {
                // Account-owned connected printers; the send path refreshes live readiness.
                if (d.status == "offline" || d.status == "Offline") continue;
                result.push_back({d.id, d.name, d.model, PrinterProvider::flashforge, true});
            }
        }
        return result;
    }
    Device device(const std::string& id) {
        for (auto d : devices()) if (d.id == id) return d;
        throw std::runtime_error("Printer disconnected. Refresh the printer list in Orca and try again.");
    }
    const Preset& profile(const Device& d, const std::string& name) {
        auto* p = wxGetApp().preset_bundle->printers.find_preset(name, false);
        if (!p || !p->loaded || !p->is_visible || p->is_default ||
            canonical_printer_model(d.provider, text_option(p->config, "printer_model")) != canonical_printer_model(d.provider, d.model))
            throw std::runtime_error("Choose a matching printer and nozzle profile.");
        const auto& edited = wxGetApp().preset_bundle->printers.get_edited_preset();
        return edited.name == p->name ? edited : *p;
    }
    bool compatible(const Preset& value, const Preset& machine) {
        return is_compatible_with_printer(value.config, value.type, machine.config, machine.name);
    }
    Json options(const PresetCollection& collection, const Preset& machine) {
        Json result = Json::array();
        const auto& edited = collection.get_edited_preset();
        if (compatible(edited, machine)) result.push_back({{"id", current}, {"name", "Current in Orca (including unsaved edits): " + edited.name}});
        for (const auto& p : collection.get_presets())
            if (p.loaded && p.is_visible && !p.is_default && compatible(p, machine))
                result.push_back({{"id", p.name}, {"name", p.name}, {"user", p.is_user()}});
        return result;
    }
    Json catalog(const Json& request) {
        auto* b = wxGetApp().preset_bundle;
        Json result = {{"printers", Json::array()}, {"profiles", Json::array()}, {"materials", Json::array()}, {"presets", Json::array()}};
        for (const auto& d : devices()) result["printers"].push_back({{"id", d.id}, {"name", d.name}, {"model", d.model}, {"ready", d.ready}});
        const auto id = request.value("printer", "");
        if (id.empty()) return result;
        auto d = device(id);
        for (const auto& p : b->printers.get_presets())
            if (p.loaded && p.is_visible && !p.is_default &&
                canonical_printer_model(d.provider, text_option(p.config, "printer_model")) == canonical_printer_model(d.provider, d.model))
                result["profiles"].push_back({{"id", p.name}, {"name", p.name}});
        const auto name = request.value("profile", "");
        if (!name.empty()) {
            const auto& p = profile(d, name);
            result["materials"] = options(b->filaments, p);
            result["presets"] = options(b->prints, p);
        }
        return result;
    }
    const Preset& choice(const PresetCollection& c, const std::string& id, const Preset& p) {
        const auto* value = id == current ? &c.get_edited_preset() : c.find_preset(id, false);
        if (!value || !compatible(*value, p)) throw std::runtime_error("The selected settings no longer match this printer. Refresh the choices.");
        return *value;
    }
    Json prepare(const Json& request, const fs::path& dir) {
        auto d = device(request.at("printer").get<std::string>());
        auto* b = wxGetApp().preset_bundle;
        const auto& p = profile(d, request.at("profile").get<std::string>());
        const auto& process = choice(b->prints, request.at("preset").get<std::string>(), p);
        const auto& filament = choice(b->filaments, request.at("material").get<std::string>(), p);
        PresetBundle copy(*b);
        copy.printers.select_preset_by_name(p.name, true);
        copy.printers.get_edited_preset().config = p.config;
        copy.prints.select_preset_by_name(process.name, true);
        copy.filaments.select_preset_by_name(filament.name, true);
        copy.prints.get_edited_preset().config = process.config;
        copy.filaments.get_edited_preset().config = filament.config;
        copy.filament_presets = {filament.name};
        copy.project_config.set_key_value("filament_colour", new ConfigOptionStrings({first_option(filament.config, "filament_colour")}));
        copy.project_config.set_key_value("filament_map", new ConfigOptionInts({1}));
        Settings s;
        s.device = d.id; s.profile = p.name; s.machine = p.config; s.process = process.config; s.filament = filament.config;
        s.full = copy.full_config_secure(); s.model_id = copy.printers.get_edited_preset().get_printer_type(&copy);
        const auto* nozzles = p.config.option<ConfigOptionFloats>("nozzle_diameter");
        if (!nozzles || nozzles->values.empty()) throw std::runtime_error("Printer has no nozzle settings");
        s.nozzle = nozzles->values.front();
        // Machine and process JSON contain only their own preset categories. Network
        // credentials are never exported to the mailbox or the command line.
        for (const auto* key : {"print_host", "print_host_webui", "printhost_apikey", "printhost_cafile", "printhost_user", "printhost_password", "printhost_port"}) s.machine.erase(key);
        write_preset(dir / "machine.json", s.machine, p.name, "machine");
        write_preset(dir / "process.json", s.process, process.name, "process");
        write_preset(dir / "filament.json", s.filament, filament.name, "filament", filament.filament_id);
        if (prepared.size() >= 128) throw std::runtime_error("This session has too many pending exports. Restart Orca to clear them.");
        const bool fast_start = VendorPrinting::is_fast_start(s.profile, text_option(s.machine, "machine_start_gcode"));
        prepared[dir.filename().u8string()] = std::move(s);
        return {{"token", dir.filename().u8string()}, {"background_supported", nozzles->values.size() == 1}, {"fast_start", fast_start},
            {"machine", (dir / "machine.json").u8string()}, {"process", (dir / "process.json").u8string()},
            {"filament", (dir / "filament.json").u8string()}};
    }
    Settings& settings(const Json& request) {
        auto it = prepared.find(request.at("token").get<std::string>());
        if (it == prepared.end()) throw std::runtime_error("Settings expired. Choose settings again.");
        device(it->second.device);
        return it->second;
    }
    fs::path artifact(const std::string& token, const char* name) {
        if (!request_id(token)) throw std::runtime_error("Invalid job identity");
        const auto dir = root / "requests" / token;
        const auto path = dir / name;
        if (fs::is_symlink(dir) || fs::is_symlink(path) || !fs::is_regular_file(path) || fs::file_size(path) == 0)
            throw std::runtime_error("Job file missing or invalid");
        return path;
    }
    Json load(const Json& request) {
        auto& s = settings(request);
        auto* plater = wxGetApp().plater();
        if (plater->is_background_process_slicing() || plater->is_background_process_update_scheduled())
            throw std::runtime_error("Orca is slicing. Wait for it to finish, then load again.");
        const auto token = request.at("token").get<std::string>();
        const auto path = artifact(token, "model.step");
        const auto source = request.at("source").get<std::string>();
        if (source.size() != 64 || !request_id(source.substr(0, 32)) || !request_id(source.substr(32)))
            throw std::runtime_error("Invalid SolidWorks part identity");
        // Track stable object IDs rather than array indexes: users may have
        // deleted/reordered objects since the previous SolidWorks export.
        const auto previous = imported[source];
        auto& model = wxGetApp().model();
        const auto indices = plater->load_files({boost::filesystem::path(path.u8string())}, LoadStrategy::LoadModel | LoadStrategy::Silence);
        if (indices.empty()) throw std::runtime_error("STEP import failed. The previous model has been retained.");
        std::vector<ObjectID> fresh;
        for (auto i : indices) fresh.push_back(model.objects.at(i)->id());
        for (std::size_t i = model.objects.size(); i-- > 0;)
            if (std::find(previous.begin(), previous.end(), model.objects[i]->id()) != previous.end()) plater->delete_object_from_model(i);
        imported[source] = std::move(fresh);
        wxGetApp().preset_bundle->load_config_model("SolidWorks session", s.full);
        wxGetApp().load_current_presets(false, false);
        wxGetApp().app_config->set("dual_vendor_selected_printer", s.device);
        if (s.device.rfind("flashforge:", 0) == 0) wxGetApp().app_config->set("flashforge_selected_printer", s.device);
        else if (auto* manager = wxGetApp().getDeviceManager()) {
            if (manager->set_selected_machine(s.device.substr(s.device.find(':') + 1)))
                MonitorPanel::on_machine_selected(manager->get_selected_machine());
        }
        wxGetApp().mainframe->Iconize(false); wxGetApp().mainframe->Show(); wxGetApp().mainframe->Raise();
        return {{"message", "Loaded in Orca. Process and filament edits are available as Current in Orca on the next export."}};
    }
    Json submit(const Json& request, const fs::path& dir) {
        if (!pending.empty()) throw std::runtime_error("A SolidWorks job is already being sent. Check its status first.");
        if (wxGetApp().app_config->get_stealth_mode()) throw std::runtime_error("Disable Stealth Mode before cloud printing.");
        auto s = settings(request);
        if (VendorPrinting::is_fast_start(s.profile, text_option(s.machine, "machine_start_gcode")) && !request.value("fast_start_checked", false))
            throw std::runtime_error("Fast Start requires the volume warning check. Update the SolidWorks add-in or load in slicer to review and print.");
        const auto* nozzles = s.machine.option<ConfigOptionFloats>("nozzle_diameter");
        if (!nozzles || nozzles->values.size() != 1 || !request.value("external_spool", false))
            throw std::runtime_error("Review material routing in Orca for multi-extruder or AMS printing.");
        const auto token = request.at("token").get<std::string>();
        auto job = artifact(token, "plate.3mf");
        auto png = artifact(token, "thumbnail.png");
        VendorPrinting::CloudPrintRequest cloud;
        cloud.printer.id = s.device; cloud.printer.model_id = s.model_id; cloud.printer.nozzle_diameter = s.nozzle;
        cloud.job_path = job.u8string(); cloud.thumbnail_path = png.u8string(); cloud.destination_name = "solidworks.3mf";
        cloud.union_id = boost::uuids::to_string(boost::uuids::random_generator()());
        cloud.print_now = true; cloud.leveling = request.value("leveling", true);
        cloud.materials.push_back({0, 0, first_option(s.filament, "filament_type"),
            first_option(s.filament, "filament_colour"), first_option(s.filament, "filament_colour")});
        auto hash = [](const std::string& path, std::string& digest) { auto file = path; return bbl_calc_md5(file, digest); };
        const auto validation = VendorPrinting::validate_flashforge_archive(cloud, hash);
        if (!validation.files) throw std::runtime_error("The sliced archive failed printer, nozzle, plate, material or thumbnail verification. Load in slicer to review.");
        if (s.device.rfind("flashforge:", 0) == 0) {
            if (!send_flashforge_cloud(cloud, VendorPrinting::make_flashforge_archive_inspector(hash)))
                throw std::runtime_error("Flashforge connection is busy or changed. Refresh it in Orca.");
            pending = dir; flash_pending = true;
        } else {
            auto d = device(s.device);
            auto* agent = wxGetApp().getAgent();
            auto* manager = wxGetApp().getDeviceManager();
            if (!d.ready || !agent || !manager || manager->get_current_printer_agent_id() != "bbl")
                throw std::runtime_error("Bambu printer is busy or its cloud connection is inactive.");
            auto route = agent->get_printer_agent(); // Own this exact route across provider switches.
            if (!route) throw std::runtime_error("Bambu print route is unavailable.");
            PrintParams params{};
            params.dev_id = s.device.substr(s.device.find(':') + 1);
            params.filename = job.u8string(); params.config_filename = job.u8string();
            params.project_name = "SolidWorks"; params.preset_name = s.profile;
            params.plate_index = 1; params.connection_type = "cloud"; params.print_type = "from_normal";
            params.task_bed_leveling = cloud.leveling; params.task_use_ams = false;
            params.ams_mapping = "[-1]";
            params.task_bed_type = bed_type_to_gcode_string(s.full.opt_enum<BedType>("curr_bed_type"));
            pending = dir; flash_pending = false; worker_done = false;
            worker = std::thread([this, route, params] {
                try { worker_result = route->start_print(params, [](int, int, std::string) {},
                    [this] { return stopping.load(); }, [](int, std::string) { return true; }); }
                catch (...) { worker_result = -1; }
                worker_done = true;
            });
        }
        return {{"pending", true}, {"message", "Sending through the slicer's cloud connection. Do not retry until the result is known."}};
    }
    void tick() {
        if (handling || wxGetApp().is_closing()) return;
        struct Guard { bool& v; Guard(bool& b) : v(b) { v = true; } ~Guard() { v = false; } } guard(handling);
        try {
            if (++ticks % 8 == 0) heartbeat();
            if (!pending.empty()) {
                if (flash_pending) {
                    const auto state = flashforge_account_snapshot();
                    if (!state.cloud_job_running) {
                        // Vendor outcome text is authoritative; submission is not physical completion.
                        write_json(pending / "completion.json", {{"ok", state.cloud_job_delivered}, {"message", state.cloud_job_message}});
                        pending.clear();
                    }
                } else if (worker_done) {
                    worker.join();
                    write_json(pending / "completion.json", {{"ok", worker_result == 0}, {"message", worker_result == 0 ?
                        "Cloud accepted the print request. Check Device for physical printer status." :
                        "Cloud submission failed or its result is uncertain. Check Device before trying again."}});
                    pending.clear();
                }
            }
            for (const auto& entry : fs::directory_iterator(root / "requests")) {
                const auto id = entry.path().filename().u8string();
                if (!request_id(id) || entry.is_symlink() || !entry.is_directory() || consumed.count(id)) continue;
                const auto file = entry.path() / "request.json";
                if (!fs::exists(file)) continue;
                std::error_code claim_error;
                const auto claimed = entry.path() / "processing.json";
                fs::rename(file, claimed, claim_error);
                if (claim_error) continue; // Client may have withdrawn a timed-out request.
                consumed.insert(id); // Never replay a print after a response-writing failure.
                Json response;
                try {
                    const auto request = read_json(claimed);
                    if (request.at("schema") != 1 || request.at("session") != session)
                        throw std::runtime_error("Slicer session changed. Reconnect from SolidWorks.");
                    const auto op = request.at("op").get<std::string>();
                    Json value;
                    if (op == "catalog") value = catalog(request);
                    else if (op == "prepare") value = prepare(request, entry.path());
                    else if (op == "load") value = load(request);
                    else if (op == "print") value = submit(request, entry.path());
                    else throw std::runtime_error("Unsupported SolidWorks bridge operation");
                    response = {{"ok", true}, {"result", value}};
                } catch (const std::exception& e) { response = {{"ok", false}, {"message", e.what()}}; }
                write_json(entry.path() / "response.json", response);
                break;
            }
        } catch (...) { /* A malformed mailbox cannot break the editor's event loop. */ }
    }
};
Bridge* instance = nullptr;
} // namespace
void start_solidworks_bridge(wxWindow* owner) {
    try { if (!instance) instance = new Bridge(owner); } catch (...) { /* Slicer remains usable if local storage is unavailable. */ }
}
void stop_solidworks_bridge() { delete instance; instance = nullptr; }
}}
