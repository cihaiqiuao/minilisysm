#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace lisysm {

struct BatteryHealthSample {
    bool valid{false};
    std::string name;
    double capacity_percent{-1.0};
    double health_percent{-1.0};
    int64_t cycle_count{-1};
    double temperature_celsius{-1.0};
};

struct StorageHealthSample {
    bool valid{false};
    std::string device;
    double lifetime_used_percent{-1.0};
    int64_t pre_eol_info{-1};
};

struct MemoryHealthSample {
    bool valid{false};
    uint64_t ecc_corrected_errors{0};
    uint64_t ecc_uncorrected_errors{0};
};

struct HardwareHealthSample {
    std::vector<BatteryHealthSample> batteries;
    std::vector<StorageHealthSample> storage_devices;
    MemoryHealthSample memory;
};

class HardwareHealthCollector {
  public:
    HardwareHealthCollector(std::string power_supply_root = "/sys/class/power_supply",
                            std::string block_root = "/sys/block", std::string edac_root = "/sys/devices/system/edac");

    HardwareHealthSample collect() const;

  private:
    std::vector<BatteryHealthSample> collect_batteries() const;
    std::vector<StorageHealthSample> collect_storage() const;
    MemoryHealthSample collect_memory() const;

    std::string power_supply_root_;
    std::string block_root_;
    std::string edac_root_;
};

} // namespace lisysm
