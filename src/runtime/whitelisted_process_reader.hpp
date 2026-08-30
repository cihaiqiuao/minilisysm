#pragma once

#include <cstdint>
#include <filesystem>
#include <functional>
#include <optional>
#include <string>
#include <vector>

namespace lisysm::detail {

struct WhitelistedProcessSample {
    int32_t pid{0};
    std::string name;
    uint64_t starttime_ticks{0};
    uint64_t cpu_ticks{0};
    uint64_t rss_bytes{0};
    uint64_t threads{0};
};

struct WhitelistedProcessScan {
    std::vector<WhitelistedProcessSample> samples;
    std::vector<int32_t> uncertain_pids;
    bool complete{false};
};

using ProcessDetailsReader =
    std::function<std::optional<WhitelistedProcessSample>(const std::filesystem::path&, int32_t, std::string)>;

std::optional<WhitelistedProcessSample> read_whitelisted_process_details(const std::filesystem::path& process_path,
                                                                          int32_t pid, std::string name);

WhitelistedProcessScan scan_whitelisted_processes(
    const std::filesystem::path& proc_root, const std::vector<std::string>& whitelist,
    ProcessDetailsReader read_details = read_whitelisted_process_details);

} // namespace lisysm::detail
