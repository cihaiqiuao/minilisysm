#include "minilisysm/collectors/hardware_health_collector.hpp"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <optional>
#include <sstream>
#include <string>
#include <utility>

namespace lisysm {
namespace {

std::optional<std::string> read_text(const std::filesystem::path& path) {
    std::ifstream input(path);
    if (!input) {
        return std::nullopt;
    }
    std::string value;
    std::getline(input, value);
    while (!value.empty() && std::isspace(static_cast<unsigned char>(value.back())) != 0) {
        value.pop_back();
    }
    return value;
}

std::optional<int64_t> read_int(const std::filesystem::path& path) {
    const std::optional<std::string> text = read_text(path);
    if (!text || text->empty()) {
        return std::nullopt;
    }
    try {
        size_t consumed = 0;
        const int64_t value = std::stoll(*text, &consumed, 0);
        return consumed > 0 ? std::optional<int64_t>(value) : std::nullopt;
    } catch (...) {
        return std::nullopt;
    }
}

std::optional<uint64_t> read_uint(const std::filesystem::path& path) {
    const std::optional<int64_t> value = read_int(path);
    if (!value || *value < 0) {
        return std::nullopt;
    }
    return static_cast<uint64_t>(*value);
}

std::optional<double> read_capacity_health_percent(const std::filesystem::path& dir) {
    std::optional<int64_t> full = read_int(dir / "charge_full");
    std::optional<int64_t> design = read_int(dir / "charge_full_design");
    if (!full || !design) {
        full = read_int(dir / "energy_full");
        design = read_int(dir / "energy_full_design");
    }
    if (!full || !design || *design <= 0) {
        return std::nullopt;
    }
    return std::max(0.0, std::min(100.0, static_cast<double>(*full) * 100.0 / static_cast<double>(*design)));
}

std::optional<double> read_power_supply_temperature(const std::filesystem::path& dir) {
    const std::optional<int64_t> temp = read_int(dir / "temp");
    if (!temp) {
        return std::nullopt;
    }
    return static_cast<double>(*temp) / 10.0;
}

std::optional<double> read_emmc_lifetime_percent(const std::filesystem::path& path) {
    const std::optional<std::string> text = read_text(path);
    if (!text) {
        return std::nullopt;
    }
    std::istringstream input(*text);
    std::string token;
    int64_t max_bucket = -1;
    while (input >> token) {
        try {
            size_t consumed = 0;
            const int64_t value = std::stoll(token, &consumed, 0);
            if (consumed > 0) {
                max_bucket = std::max(max_bucket, value);
            }
        } catch (...) {
            continue;
        }
    }
    if (max_bucket < 0) {
        return std::nullopt;
    }
    if (max_bucket >= 0x0b) {
        return 100.0;
    }
    return std::max(0.0, std::min(100.0, static_cast<double>(max_bucket) * 10.0));
}

void add_edac_counts(const std::filesystem::path& root, uint64_t& corrected, uint64_t& uncorrected) {
    std::error_code ec;
    if (!std::filesystem::exists(root, ec)) {
        return;
    }
    for (const auto& entry : std::filesystem::recursive_directory_iterator(root, ec)) {
        if (ec || !entry.is_regular_file(ec)) {
            continue;
        }
        const std::string filename = entry.path().filename().string();
        if (filename == "ce_count") {
            corrected += read_uint(entry.path()).value_or(0);
        } else if (filename == "ue_count") {
            uncorrected += read_uint(entry.path()).value_or(0);
        }
    }
}

} // namespace

HardwareHealthCollector::HardwareHealthCollector(std::string power_supply_root, std::string block_root,
                                                 std::string edac_root)
    : power_supply_root_(std::move(power_supply_root)), block_root_(std::move(block_root)),
      edac_root_(std::move(edac_root)) {}

HardwareHealthSample HardwareHealthCollector::collect() const {
    HardwareHealthSample sample;
    sample.batteries = collect_batteries();
    sample.storage_devices = collect_storage();
    sample.memory = collect_memory();
    return sample;
}

std::vector<BatteryHealthSample> HardwareHealthCollector::collect_batteries() const {
    std::vector<BatteryHealthSample> samples;
    std::error_code ec;
    if (!std::filesystem::exists(power_supply_root_, ec)) {
        return samples;
    }
    for (const auto& entry : std::filesystem::directory_iterator(power_supply_root_, ec)) {
        if (ec || !entry.is_directory(ec)) {
            continue;
        }
        const std::filesystem::path& dir = entry.path();
        const std::optional<std::string> type = read_text(dir / "type");
        if (!type || *type != "Battery") {
            continue;
        }
        BatteryHealthSample sample;
        sample.name = dir.filename().string();
        if (const std::optional<int64_t> capacity = read_int(dir / "capacity")) {
            sample.capacity_percent = static_cast<double>(*capacity);
            sample.valid = true;
        }
        if (const std::optional<double> health = read_capacity_health_percent(dir)) {
            sample.health_percent = *health;
            sample.valid = true;
        }
        if (const std::optional<int64_t> cycle = read_int(dir / "cycle_count")) {
            sample.cycle_count = *cycle;
            sample.valid = true;
        }
        if (const std::optional<double> temp = read_power_supply_temperature(dir)) {
            sample.temperature_celsius = *temp;
            sample.valid = true;
        }
        if (sample.valid) {
            samples.push_back(std::move(sample));
        }
    }
    return samples;
}

std::vector<StorageHealthSample> HardwareHealthCollector::collect_storage() const {
    std::vector<StorageHealthSample> samples;
    std::error_code ec;
    if (!std::filesystem::exists(block_root_, ec)) {
        return samples;
    }
    for (const auto& entry : std::filesystem::directory_iterator(block_root_, ec)) {
        if (ec || !entry.is_directory(ec)) {
            continue;
        }
        const std::filesystem::path& dir = entry.path();
        const std::filesystem::path device_dir = dir / "device";
        StorageHealthSample sample;
        sample.device = dir.filename().string();
        if (const std::optional<double> lifetime = read_emmc_lifetime_percent(device_dir / "life_time")) {
            sample.lifetime_used_percent = *lifetime;
            sample.valid = true;
        }
        if (const std::optional<int64_t> pre_eol = read_int(device_dir / "pre_eol_info")) {
            sample.pre_eol_info = *pre_eol;
            sample.valid = true;
        }
        if (sample.valid) {
            samples.push_back(std::move(sample));
        }
    }
    return samples;
}

MemoryHealthSample HardwareHealthCollector::collect_memory() const {
    MemoryHealthSample sample;
    add_edac_counts(edac_root_, sample.ecc_corrected_errors, sample.ecc_uncorrected_errors);
    sample.valid = sample.ecc_corrected_errors > 0 || sample.ecc_uncorrected_errors > 0;
    std::error_code ec;
    if (!sample.valid && std::filesystem::exists(edac_root_, ec)) {
        sample.valid = true;
    }
    return sample;
}

} // namespace lisysm
