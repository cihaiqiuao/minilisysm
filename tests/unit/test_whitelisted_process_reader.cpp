#include "runtime/whitelisted_process_reader.hpp"

#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <optional>
#include <string>
#include <vector>

#define CHECK(condition)                                                                                               \
    do {                                                                                                               \
        if (!(condition)) {                                                                                            \
            std::cerr << "check failed: " #condition << " at line " << __LINE__ << "\n";                               \
            return EXIT_FAILURE;                                                                                       \
        }                                                                                                              \
    } while (false)

namespace {

namespace fs = std::filesystem;

class TempDirectory {
  public:
    TempDirectory()
        : path_(fs::temp_directory_path() /
                ("minilisysm-process-reader-test-" +
                 std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()))) {
        std::error_code error;
        created_ = fs::create_directories(path_, error) && !error;
    }

    ~TempDirectory() {
        std::error_code ignored;
        fs::remove_all(path_, ignored);
    }

    bool created() const {
        return created_;
    }

    const fs::path& path() const {
        return path_;
    }

  private:
    fs::path path_;
    bool created_{false};
};

bool write_file(const fs::path& path, const std::string& content) {
    std::ofstream output(path);
    output << content;
    output.close();
    return output.good();
}

} // namespace

int main() {
    TempDirectory temp_directory;
    CHECK(temp_directory.created());

    const fs::path noise = temp_directory.path() / "101";
    const fs::path target = temp_directory.path() / "202";
    CHECK(fs::create_directories(noise));
    CHECK(fs::create_directories(target / "task" / "202"));
    CHECK(fs::create_directories(target / "task" / "203"));
    CHECK(write_file(noise / "comm", "noise\n"));
    CHECK(write_file(target / "comm", "target\n"));
    CHECK(write_file(target / "stat", "202 (target) S 1 2 3 4 5 6 7 8 9 10 11 7 14 15 16 17 2 19 12345\n"));
    CHECK(write_file(target / "status", "Name:\ttarget\nVmRSS:\t2048 kB\n"));

    size_t details_calls = 0;
    std::vector<fs::path> details_paths;
    auto result = lisysm::detail::scan_whitelisted_processes(
        temp_directory.path(), {"target"}, [&](const fs::path& process_path, int32_t pid, std::string name) {
            ++details_calls;
            details_paths.push_back(process_path);
            return lisysm::detail::read_whitelisted_process_details(process_path, pid, std::move(name));
        });

    CHECK(result.complete);
    CHECK(result.uncertain_pids.empty());
    CHECK(details_calls == 1);
    CHECK(details_paths.size() == 1);
    CHECK(details_paths.front().filename() == "202");
    CHECK(result.samples.size() == 1);
    const auto& sample = result.samples.front();
    CHECK(sample.pid == 202);
    CHECK(sample.name == "target");
    CHECK(sample.starttime_ticks == 12345);
    CHECK(sample.cpu_ticks == 18);
    CHECK(sample.rss_bytes == 2ULL * 1024ULL * 1024ULL);
    CHECK(sample.threads == 2);

    auto incomplete = lisysm::detail::scan_whitelisted_processes(
        temp_directory.path(), {"target"},
        [](const fs::path&, int32_t, const std::string&) -> std::optional<lisysm::detail::WhitelistedProcessSample> {
            return std::nullopt;
        });
    CHECK(incomplete.samples.empty());
    CHECK(incomplete.complete);
    CHECK(incomplete.uncertain_pids.size() == 1);
    CHECK(incomplete.uncertain_pids.front() == 202);

    const fs::path missing_comm = temp_directory.path() / "303";
    CHECK(fs::create_directories(missing_comm));
    auto missing_comm_scan = lisysm::detail::scan_whitelisted_processes(temp_directory.path(), {"target"});
    CHECK(missing_comm_scan.complete);
    CHECK(missing_comm_scan.uncertain_pids.size() == 1);
    CHECK(missing_comm_scan.uncertain_pids.front() == 303);
    CHECK(fs::remove_all(missing_comm) == 1);

    const fs::path invalid_pid_entry = temp_directory.path() / "404";
    CHECK(write_file(invalid_pid_entry, "not a directory\n"));
    auto invalid_pid_scan = lisysm::detail::scan_whitelisted_processes(temp_directory.path(), {"target"});
    CHECK(invalid_pid_scan.complete);
    CHECK(invalid_pid_scan.uncertain_pids.size() == 1);
    CHECK(invalid_pid_scan.uncertain_pids.front() == 404);
    CHECK(fs::remove(invalid_pid_entry));

    const fs::path early_process = temp_directory.path() / "505";
    CHECK(fs::create_directories(early_process / "task" / "505"));
    CHECK(write_file(early_process / "stat", "505 (early) S 1 2 3 4 5 6 7 8 9 10 1 1 14 15 16 17 1 19 0\n"));
    CHECK(write_file(early_process / "status", "Name:\tearly\nVmRSS:\t1 kB\n"));
    auto early_sample = lisysm::detail::read_whitelisted_process_details(early_process, 505, "early");
    CHECK(early_sample.has_value());
    CHECK(early_sample->starttime_ticks == 0);

    const fs::path missing_status_process = temp_directory.path() / "606";
    CHECK(fs::create_directories(missing_status_process / "task" / "606"));
    CHECK(write_file(missing_status_process / "stat",
                     "606 (missing-status) S 1 2 3 4 5 6 7 8 9 10 1 1 14 15 16 17 1 19 6060\n"));
    CHECK(!lisysm::detail::read_whitelisted_process_details(missing_status_process, 606, "missing-status").has_value());

    const fs::path unreadable_task_process = temp_directory.path() / "707";
    CHECK(fs::create_directories(unreadable_task_process));
    CHECK(write_file(unreadable_task_process / "task", "not a directory\n"));
    CHECK(write_file(unreadable_task_process / "stat",
                     "707 (unreadable-task) S 1 2 3 4 5 6 7 8 9 10 1 1 14 15 16 17 1 19 7070\n"));
    CHECK(write_file(unreadable_task_process / "status", "Name:\tunreadable-task\nVmRSS:\t1 kB\n"));
    CHECK(
        !lisysm::detail::read_whitelisted_process_details(unreadable_task_process, 707, "unreadable-task").has_value());

    const fs::path missing_rss_process = temp_directory.path() / "808";
    CHECK(fs::create_directories(missing_rss_process / "task" / "808"));
    CHECK(write_file(missing_rss_process / "stat",
                     "808 (missing-rss) S 1 2 3 4 5 6 7 8 9 10 1 1 14 15 16 17 1 19 8080\n"));
    CHECK(write_file(missing_rss_process / "status", "Name:\tmissing-rss\n"));
    CHECK(!lisysm::detail::read_whitelisted_process_details(missing_rss_process, 808, "missing-rss").has_value());

    const fs::path malformed_rss_process = temp_directory.path() / "909";
    CHECK(fs::create_directories(malformed_rss_process / "task" / "909"));
    CHECK(write_file(malformed_rss_process / "stat",
                     "909 (malformed-rss) S 1 2 3 4 5 6 7 8 9 10 1 1 14 15 16 17 1 19 9090\n"));
    CHECK(write_file(malformed_rss_process / "status", "Name:\tmalformed-rss\nVmRSS:\tnot-a-number kB\n"));
    CHECK(!lisysm::detail::read_whitelisted_process_details(malformed_rss_process, 909, "malformed-rss").has_value());

    auto missing_root = lisysm::detail::scan_whitelisted_processes(temp_directory.path() / "missing", {"target"});
    CHECK(!missing_root.complete);
    return EXIT_SUCCESS;
}
