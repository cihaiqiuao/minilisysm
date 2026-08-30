#include "minilisysm/collectors/hardware_health_collector.hpp"

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>

#define CHECK(condition)                                                                                               \
    do {                                                                                                               \
        if (!(condition)) {                                                                                            \
            std::cerr << "check failed: " #condition << " at line " << __LINE__ << "\n";                               \
            return EXIT_FAILURE;                                                                                       \
        }                                                                                                              \
    } while (false)

namespace {

void write_file(const std::filesystem::path& path, const std::string& content) {
    std::filesystem::create_directories(path.parent_path());
    std::ofstream output(path);
    output << content;
}

} // namespace

int main() {
    const std::filesystem::path root = std::filesystem::temp_directory_path() / "minilisysm_hw_health_test";
    std::filesystem::remove_all(root);

    const std::filesystem::path power = root / "power_supply";
    const std::filesystem::path block = root / "block";
    const std::filesystem::path edac = root / "edac";

    write_file(power / "BAT0" / "type", "Battery\n");
    write_file(power / "BAT0" / "capacity", "78\n");
    write_file(power / "BAT0" / "cycle_count", "321\n");
    write_file(power / "BAT0" / "charge_full", "4200000\n");
    write_file(power / "BAT0" / "charge_full_design", "5000000\n");
    write_file(power / "BAT0" / "temp", "315\n");
    write_file(power / "AC" / "type", "Mains\n");

    write_file(block / "mmcblk0" / "device" / "life_time", "0x02 0x04\n");
    write_file(block / "mmcblk0" / "device" / "pre_eol_info", "0x01\n");

    write_file(edac / "mc0" / "ce_count", "7\n");
    write_file(edac / "mc0" / "ue_count", "1\n");

    const lisysm::HardwareHealthCollector collector(power.string(), block.string(), edac.string());
    const lisysm::HardwareHealthSample sample = collector.collect();

    CHECK(sample.batteries.size() == 1);
    CHECK(sample.batteries[0].valid);
    CHECK(sample.batteries[0].name == "BAT0");
    CHECK(sample.batteries[0].capacity_percent == 78.0);
    CHECK(sample.batteries[0].cycle_count == 321);
    CHECK(sample.batteries[0].health_percent > 83.9 && sample.batteries[0].health_percent < 84.1);
    CHECK(sample.batteries[0].temperature_celsius > 31.4 && sample.batteries[0].temperature_celsius < 31.6);

    CHECK(sample.storage_devices.size() == 1);
    CHECK(sample.storage_devices[0].valid);
    CHECK(sample.storage_devices[0].device == "mmcblk0");
    CHECK(sample.storage_devices[0].lifetime_used_percent == 40.0);
    CHECK(sample.storage_devices[0].pre_eol_info == 1);

    CHECK(sample.memory.valid);
    CHECK(sample.memory.ecc_corrected_errors == 7);
    CHECK(sample.memory.ecc_uncorrected_errors == 1);

    const lisysm::HardwareHealthCollector empty_collector(
        (root / "missing_power").string(), (root / "missing_block").string(), (root / "missing_edac").string());
    const lisysm::HardwareHealthSample empty = empty_collector.collect();
    CHECK(empty.batteries.empty());
    CHECK(empty.storage_devices.empty());
    CHECK(!empty.memory.valid);

    std::filesystem::remove_all(root);
    return EXIT_SUCCESS;
}
