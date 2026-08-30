#include "whitelisted_process_reader.hpp"

#include <algorithm>
#include <charconv>
#include <fstream>
#include <limits>
#include <sstream>
#include <system_error>
#include <utility>

namespace lisysm::detail {
namespace {

std::optional<int32_t> parse_pid(const std::filesystem::path& process_path) {
    const std::string filename = process_path.filename().string();
    int32_t pid = 0;
    const auto parsed = std::from_chars(filename.data(), filename.data() + filename.size(), pid);
    if (parsed.ec != std::errc{} || parsed.ptr != filename.data() + filename.size() || pid <= 0) {
        return std::nullopt;
    }
    return pid;
}

} // namespace

std::optional<WhitelistedProcessSample> read_whitelisted_process_details(const std::filesystem::path& process_path,
                                                                          int32_t pid, std::string name) {
    std::ifstream stat_file(process_path / "stat");
    std::string stat;
    if (!std::getline(stat_file, stat)) {
        return std::nullopt;
    }
    const size_t close_paren = stat.rfind(") ");
    if (close_paren == std::string::npos) {
        return std::nullopt;
    }

    std::istringstream fields(stat.substr(close_paren + 2));
    std::string field;
    uint64_t utime = 0;
    uint64_t stime = 0;
    uint64_t starttime = 0;
    bool found_starttime = false;
    try {
        for (int index = 0; fields >> field; ++index) {
            if (index == 11) {
                utime = std::stoull(field);
            } else if (index == 12) {
                stime = std::stoull(field);
            } else if (index == 19) {
                starttime = std::stoull(field);
                found_starttime = true;
                break;
            }
        }
    } catch (...) {
        return std::nullopt;
    }
    if (!found_starttime) {
        return std::nullopt;
    }

    std::error_code task_error;
    uint64_t threads = 0;
    std::filesystem::directory_iterator task(process_path / "task",
                                             std::filesystem::directory_options::skip_permission_denied, task_error);
    const std::filesystem::directory_iterator end;
    if (task_error) {
        return std::nullopt;
    }
    while (!task_error && task != end) {
        std::error_code entry_error;
        if (!task->is_directory(entry_error) || entry_error) {
            return std::nullopt;
        }
        ++threads;
        task.increment(task_error);
    }
    if (task_error || threads == 0) {
        return std::nullopt;
    }

    std::ifstream status_file(process_path / "status");
    if (!status_file.is_open()) {
        return std::nullopt;
    }
    uint64_t rss_bytes = 0;
    bool found_rss = false;
    std::string line;
    while (std::getline(status_file, line)) {
        if (line.rfind("VmRSS:", 0) == 0) {
            std::istringstream value(line.substr(6));
            uint64_t rss_kb = 0;
            std::string unit;
            if (!(value >> rss_kb >> unit) || unit != "kB" ||
                rss_kb > std::numeric_limits<uint64_t>::max() / 1024) {
                return std::nullopt;
            }
            rss_bytes = rss_kb * 1024;
            found_rss = true;
            break;
        }
    }
    if (status_file.bad() || !found_rss) {
        return std::nullopt;
    }

    return WhitelistedProcessSample{pid, std::move(name), starttime, utime + stime, rss_bytes, threads};
}

WhitelistedProcessScan scan_whitelisted_processes(const std::filesystem::path& proc_root,
                                                   const std::vector<std::string>& whitelist,
                                                   ProcessDetailsReader read_details) {
    WhitelistedProcessScan result;
    std::error_code iterator_error;
    std::filesystem::directory_iterator current(proc_root, std::filesystem::directory_options::skip_permission_denied,
                                                iterator_error);
    const std::filesystem::directory_iterator end;
    while (!iterator_error && current != end) {
        const auto pid = parse_pid(current->path());
        if (pid) {
            std::error_code entry_error;
            if (!current->is_directory(entry_error) || entry_error) {
                result.uncertain_pids.push_back(*pid);
            } else {
                std::ifstream comm_file(current->path() / "comm");
                std::string name;
                if (!std::getline(comm_file, name) || name.empty()) {
                    result.uncertain_pids.push_back(*pid);
                } else if (std::find(whitelist.begin(), whitelist.end(), name) != whitelist.end()) {
                    auto sample = read_details(current->path(), *pid, name);
                    if (sample) {
                        result.samples.push_back(std::move(*sample));
                    } else {
                        result.uncertain_pids.push_back(*pid);
                    }
                }
            }
        }
        current.increment(iterator_error);
    }
    result.complete = !iterator_error;
    return result;
}

} // namespace lisysm::detail
