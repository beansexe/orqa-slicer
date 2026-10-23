#include "FlashforgeArchiveInspector.hpp"
#include "FlashforgeCloudPrint.hpp"
#include <miniz.h>
#include <expat.h>
#include <png.h>
#include <algorithm>
#include <array>
#include <charconv>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <functional>
#include <ios>
#include <istream>
#include <limits>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <system_error>
#include <utility>
#include <vector>

namespace Slic3r::VendorPrinting {
namespace {
constexpr std::uint64_t archive_limit = 1024ULL * 1024 * 1024;
constexpr std::uint64_t entry_limit = 256ULL * 1024 * 1024;
constexpr std::size_t xml_limit = 2 * 1024 * 1024;
constexpr std::size_t png_limit = 8 * 1024 * 1024;
constexpr std::size_t allocation_limit = 16 * 1024 * 1024;

struct Snapshot { std::uint64_t size; std::filesystem::file_time_type time; };
std::optional<Snapshot> snapshot(const std::filesystem::path& path, std::uint64_t limit)
{
    if (!path.is_absolute() || !std::filesystem::is_regular_file(path)) return std::nullopt;
    const auto size = std::filesystem::file_size(path);
    if (size == 0 || size > limit) return std::nullopt;
    return Snapshot{size, std::filesystem::last_write_time(path)};
}
bool unchanged(const Snapshot& a, const std::optional<Snapshot>& b)
{
    return b && a.size == b->size && a.time == b->time;
}
bool digest_valid(const std::string& value)
{
    if (value.size() != 32) return false;
    for (char c : value)
        if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F'))) return false;
    return true;
}

struct MemoryBudget {
    struct alignas(std::max_align_t) Block { std::size_t size; };
    std::size_t used = 0;
    static void release(void* context, void* address) noexcept
    {
        if (!address) return;
        auto& self = *static_cast<MemoryBudget*>(context);
        auto* block = static_cast<Block*>(address) - 1;
        self.used -= block->size;
        std::free(block);
    }
    static void* resize(void* context, void* address, std::size_t items, std::size_t item_size) noexcept
    {
        auto& self = *static_cast<MemoryBudget*>(context);
        if (item_size && items > allocation_limit / item_size) return nullptr;
        const auto bytes = items * item_size;
        if (!bytes) { release(context, address); return nullptr; }
        auto* old = address ? static_cast<Block*>(address) - 1 : nullptr;
        const auto previous = old ? old->size : 0;
        if (bytes > allocation_limit - (self.used - previous)) return nullptr;
        auto* block = static_cast<Block*>(std::realloc(old, bytes + sizeof(Block)));
        if (!block) return nullptr;
        block->size = bytes;
        self.used = self.used - previous + bytes;
        return block + 1;
    }
    static void* allocate(void* context, std::size_t items, std::size_t size) noexcept
    { return resize(context, nullptr, items, size); }
};

struct ZipReader {
    MemoryBudget memory;
    std::ifstream file;
    std::uint64_t size;
    mz_zip_archive zip{};
    bool opened = false;
    ZipReader(const std::filesystem::path& path, std::uint64_t bytes) : file(path, std::ios::binary), size(bytes)
    {
        zip.m_pAlloc = MemoryBudget::allocate; zip.m_pRealloc = MemoryBudget::resize;
        zip.m_pFree = MemoryBudget::release; zip.m_pAlloc_opaque = &memory;
        zip.m_pRead = read; zip.m_pIO_opaque = this;
        opened = file && mz_zip_reader_init(&zip, size, 0);
    }
    ~ZipReader() { if (opened) mz_zip_reader_end(&zip); }
    static std::size_t read(void* context, mz_uint64 offset, void* data, std::size_t count) noexcept
    {
        auto& self = *static_cast<ZipReader*>(context);
        if (offset > self.size || count > self.size - offset ||
            offset > static_cast<mz_uint64>(std::numeric_limits<std::streamoff>::max()) ||
            count > static_cast<std::size_t>(std::numeric_limits<std::streamsize>::max())) return 0;
        try {
            self.file.clear(); self.file.seekg(static_cast<std::streamoff>(offset));
            self.file.read(static_cast<char*>(data), static_cast<std::streamsize>(count));
            return static_cast<std::size_t>(self.file.gcount());
        } catch (...) { return 0; }
    }
};

bool safe_name(const std::string& name)
{
    if (name.empty() || name.front() == '/' || name.find_first_of("\\:") != std::string::npos) return false;
    for (unsigned char c : name) if (c < 32 || c == 127) return false;
    std::size_t start = 0;
    while (start < name.size()) {
        const auto end = name.find('/', start);
        const auto part = name.substr(start, end == std::string::npos ? end : end - start);
        if (part.empty() || part == "." || part == "..") return false;
        if (end == std::string::npos) break;
        start = end + 1;
    }
    return true;
}

using Entries = std::map<std::string, mz_zip_archive_file_stat>;
std::optional<Entries> entries(ZipReader& reader)
{
    const auto count = mz_zip_reader_get_num_files(&reader.zip);
    if (count == 0 || count > 4096) return std::nullopt;
    Entries result;
    std::map<std::string, bool> normalized;
    std::uint64_t total = 0;
    for (mz_uint i = 0; i < count; ++i) {
        mz_zip_archive_file_stat stat{};
        std::array<char, 513> name{};
        const auto required = mz_zip_reader_get_filename(&reader.zip, i, nullptr, 0);
        if (required <= 1 || required > name.size() || !mz_zip_reader_file_stat(&reader.zip, i, &stat) ||
            stat.m_is_encrypted || !stat.m_is_supported || stat.m_uncomp_size > entry_limit ||
            stat.m_uncomp_size > archive_limit - total || !mz_zip_validate_file(&reader.zip, i, MZ_ZIP_FLAG_VALIDATE_HEADERS_ONLY)) return std::nullopt;
        total += stat.m_uncomp_size;
        if (mz_zip_reader_get_filename(&reader.zip, i, name.data(), static_cast<mz_uint>(name.size())) != required) return std::nullopt;
        std::string filename(name.data(), required - 1);
        if (!safe_name(filename)) return std::nullopt;
        std::array<char, 513> alias{};
        const auto alias_size = mz_zip_reader_get_filename_from_extra(&reader.zip, i, alias.data(), static_cast<mz_uint>(alias.size()));
        if (alias_size && (alias_size >= alias.size() - 1 || std::string(alias.data(), alias_size) != filename)) return std::nullopt;
        auto lower = filename;
        for (char& c : lower) if (c >= 'A' && c <= 'Z') c = static_cast<char>(c + ('a' - 'A'));
        if (!normalized.emplace(lower, true).second || !result.emplace(filename, stat).second) return std::nullopt;
    }
    return result;
}

std::optional<std::string> extract(ZipReader& reader, const Entries& entries_value,
                                 const std::string& name, std::size_t limit)
{
    const auto found = entries_value.find(name);
    if (found == entries_value.end() || found->second.m_is_directory || found->second.m_uncomp_size == 0 ||
        found->second.m_uncomp_size > limit) return std::nullopt;
    std::string value(static_cast<std::size_t>(found->second.m_uncomp_size), '\0');
    if (!mz_zip_reader_extract_to_mem(&reader.zip, found->second.m_file_index, value.data(), value.size(), 0)) return std::nullopt;
    return value;
}

using Metadata = std::map<std::string, std::string>;
struct XmlDocument {
    XML_Parser parser;
    bool valid = true;
    std::string root;
    std::string index_key;
    std::vector<std::string> stack;
    std::map<int, Metadata> plates;
    Metadata plate;
    explicit XmlDocument(std::string root_value, std::string index) : parser(XML_ParserCreate(nullptr)), root(std::move(root_value)), index_key(std::move(index)) {}
    ~XmlDocument() { if (parser) XML_ParserFree(parser); }
    void reject() { valid = false; XML_StopParser(parser, XML_FALSE); }
    static void XMLCALL start(void* context, const XML_Char* name, const XML_Char** attrs) noexcept
    {
        auto& self = *static_cast<XmlDocument*>(context);
        try {
            if (self.stack.size() >= 32 || (self.stack.empty() && self.root != name)) { self.reject(); return; }
            if (std::string(name) == "plate") {
                if (self.stack.size() != 1 || self.stack[0] != "config") { self.reject(); return; }
                self.plate.clear();
            }
            if (std::string(name) == "metadata" && self.stack.size() == 2 && self.stack.back() == "plate") {
                std::string key, value;
                for (auto attr = attrs; attr && *attr; attr += 2) {
                    if (std::string(attr[0]) == "key") key = attr[1];
                    if (std::string(attr[0]) == "value") value = attr[1];
                }
                if (key.empty() || key.size() > 256 || value.size() > 8192 || self.plate.size() >= 128 ||
                    !self.plate.emplace(std::move(key), std::move(value)).second) { self.reject(); return; }
            }
            self.stack.emplace_back(name);
        } catch (...) { self.reject(); }
    }
    static void XMLCALL end(void* context, const XML_Char* name) noexcept
    {
        auto& self = *static_cast<XmlDocument*>(context);
        try {
            if (std::string(name) == "plate") {
                const auto value = self.plate.find(self.index_key);
                int index = 0;
                if (value == self.plate.end()) { self.reject(); return; }
                const auto parsed = std::from_chars(value->second.data(), value->second.data() + value->second.size(), index);
                if (parsed.ec != std::errc{} || parsed.ptr != value->second.data() + value->second.size() || index < 1 || index > 10000 ||
                    self.plates.size() >= 64 || !self.plates.emplace(index, self.plate).second) { self.reject(); return; }
            }
            self.stack.pop_back();
        } catch (...) { self.reject(); }
    }
    static void XMLCALL doctype(void* context, const XML_Char*, const XML_Char*, const XML_Char*, int) noexcept
    { static_cast<XmlDocument*>(context)->reject(); }
    bool parse(const std::string& value)
    {
        if (!parser) return false;
        XML_SetUserData(parser, this); XML_SetElementHandler(parser, start, end);
        XML_SetStartDoctypeDeclHandler(parser, doctype); XML_SetParamEntityParsing(parser, XML_PARAM_ENTITY_PARSING_NEVER);
        return XML_Parse(parser, value.data(), static_cast<int>(value.size()), XML_TRUE) == XML_STATUS_OK && valid;
    }
};

bool png_valid(const std::string& encoded)
{
    png_image image{}; image.version = PNG_IMAGE_VERSION;
    const auto cleanup = [](png_image* value) { png_image_free(value); };
    std::unique_ptr<png_image, decltype(cleanup)> guard(&image, cleanup);
    if (encoded.empty() || encoded.size() > png_limit || !png_image_begin_read_from_memory(&image, encoded.data(), encoded.size()) ||
        image.width == 0 || image.height == 0 || image.width > 2048 || image.height > 2048) return false;
    image.format = PNG_FORMAT_RGBA;
    std::vector<png_byte> pixels(PNG_IMAGE_SIZE(image));
    return png_image_finish_read(&image, nullptr, pixels.data(), 0, nullptr) != 0;
}

struct GcodeCheck {
    bool motion = false;
    std::string line;
    std::uint64_t offset = 0;
    void finish_line()
    {
        const auto first = line.find_first_not_of(" \t\r");
        if (first != std::string::npos && line.size() > first + 2 && line[first] == 'G' &&
            (line[first + 1] == '0' || line[first + 1] == '1') && (line[first + 2] == ' ' || line[first + 2] == '\t')) motion = true;
        line.clear();
    }
    static std::size_t write(void* context, mz_uint64 position, const void* data, std::size_t count) noexcept
    {
        auto& self = *static_cast<GcodeCheck*>(context);
        if (position != self.offset || count > entry_limit - self.offset) return 0;
        try {
            const auto* bytes = static_cast<const unsigned char*>(data);
            for (std::size_t i = 0; i < count; ++i) {
                const auto c = bytes[i];
                if (c == '\n') self.finish_line();
                else if (c == 0 || (c < 32 && c != '\r' && c != '\t') || self.line.size() >= 8192) return 0;
                else self.line.push_back(static_cast<char>(c));
            }
            self.offset += count;
            return count;
        } catch (...) { return 0; }
    }
};

CloudArchiveValidation inspect(const CloudPrintRequest& request, const CloudMd5Reader& hash)
{
    if (!hash || request.plate_number < 0 || request.plate_number >= 10000) return {{}, CloudArchiveError::invalid_request};
    const auto job_path = std::filesystem::u8path(request.job_path), thumb_path = std::filesystem::u8path(request.thumbnail_path);
    if (job_path.extension() != ".3mf" || thumb_path.extension() != ".png") return {{}, CloudArchiveError::invalid_request};
    const auto job_before = snapshot(job_path, archive_limit), thumb_before = snapshot(thumb_path, png_limit);
    if (!job_before || !thumb_before) return {{}, CloudArchiveError::unreadable_files};
    std::string job_hash, thumb_hash;
    if (!hash(request.job_path, job_hash) || !hash(request.thumbnail_path, thumb_hash) || !digest_valid(job_hash) || !digest_valid(thumb_hash)) return {{}, CloudArchiveError::checksum_failed};
    ZipReader reader(job_path, job_before->size);
    if (!reader.opened) return {{}, CloudArchiveError::invalid_archive};
    const auto files = entries(reader);
    if (!files) return {{}, CloudArchiveError::invalid_archive};
    const auto content = extract(reader, *files, "[Content_Types].xml", xml_limit);
    const auto model = extract(reader, *files, "Metadata/model_settings.config", xml_limit);
    const auto slice = extract(reader, *files, "Metadata/slice_info.config", xml_limit);
    if (!content || !model || !slice) return {{}, CloudArchiveError::missing_metadata};
    XmlDocument types("Types", ""), models("config", "plater_id"), slices("config", "index");
    if (!types.parse(*content) || !models.parse(*model) || !slices.parse(*slice)) return {{}, CloudArchiveError::invalid_metadata};
    const auto plate_index = request.plate_number + 1;
    const auto model_plate = models.plates.find(plate_index), slice_plate = slices.plates.find(plate_index);
    if (model_plate == models.plates.end() || slice_plate == slices.plates.end()) return {{}, CloudArchiveError::missing_plate};
    const auto gcode_name = "Metadata/plate_" + std::to_string(plate_index) + ".gcode";
    const auto gcode_ref = model_plate->second.find("gcode_file"), model_id = slice_plate->second.find("printer_model_id"), nozzle = slice_plate->second.find("nozzle_diameters");
    if (gcode_ref == model_plate->second.end() || gcode_ref->second != gcode_name || model_id == slice_plate->second.end() ||
        nozzle == slice_plate->second.end()) return {{}, CloudArchiveError::invalid_metadata};
    if (model_id->second != request.printer.model_id) return {{}, CloudArchiveError::model_mismatch};
    double diameter = 0.0;
    const auto parsed = std::from_chars(nozzle->second.data(), nozzle->second.data() + nozzle->second.size(), diameter);
    if (parsed.ec != std::errc{} || parsed.ptr != nozzle->second.data() + nozzle->second.size() || !std::isfinite(diameter) ||
        diameter < 0.1 || diameter > 2.0 || !std::isfinite(request.printer.nozzle_diameter) ||
        std::abs(diameter - request.printer.nozzle_diameter) >= 0.0001) return {{}, CloudArchiveError::nozzle_mismatch};
    const auto gcode = files->find(gcode_name);
    if (gcode == files->end() || gcode->second.m_is_directory || gcode->second.m_uncomp_size == 0) return {{}, CloudArchiveError::missing_gcode};
    GcodeCheck check;
    if (!mz_zip_reader_extract_to_callback(&reader.zip, gcode->second.m_file_index, GcodeCheck::write, &check, 0)) return {{}, CloudArchiveError::invalid_gcode};
    check.finish_line();
    if (!check.motion || check.offset != gcode->second.m_uncomp_size) return {{}, CloudArchiveError::invalid_gcode};
    std::ifstream thumb_file(thumb_path, std::ios::binary);
    std::string thumb(static_cast<std::size_t>(thumb_before->size), '\0');
    thumb_file.read(thumb.data(), static_cast<std::streamsize>(thumb.size()));
    if (!thumb_file || !png_valid(thumb)) return {{}, CloudArchiveError::invalid_thumbnail};
    std::string job_after_hash, thumb_after_hash;
    if (!hash(request.job_path, job_after_hash) || !hash(request.thumbnail_path, thumb_after_hash) || job_hash != job_after_hash || thumb_hash != thumb_after_hash ||
        !unchanged(*job_before, snapshot(job_path, archive_limit)) || !unchanged(*thumb_before, snapshot(thumb_path, png_limit))) return {{}, CloudArchiveError::changed_files};
    return {ValidatedCloudFiles{job_before->size, thumb_before->size, job_hash, thumb_hash, model_id->second, diameter, request.plate_number}, CloudArchiveError::none};
}
}

CloudArchiveValidation validate_flashforge_archive(const CloudPrintRequest& request, const CloudMd5Reader& md5_reader)
{
    try { return inspect(request, md5_reader); }
    catch (...) { return {{}, CloudArchiveError::read_failed}; }
}

CloudFileInspector make_flashforge_archive_inspector(CloudMd5Reader md5_reader)
{
    return [hash = std::move(md5_reader)](const CloudPrintRequest& request) -> std::optional<ValidatedCloudFiles> {
        return validate_flashforge_archive(request, hash).files;
    };
}

} // namespace Slic3r::VendorPrinting
