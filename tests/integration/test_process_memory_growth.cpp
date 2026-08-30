#include "minilisysm/core/config.hpp"
#include "minilisysm/runtime/monitor.hpp"

#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <thread>
#include <vector>

#if defined(__linux__)
#include <cerrno>
#include <csignal>
#include <poll.h>
#include <sys/prctl.h>
#include <sys/wait.h>
#include <unistd.h>
#endif

#if !defined(__linux__)

int main() {
    std::cout << "process memory growth integration test skipped: Linux only\n";
    return 77;
}

#else

namespace fs = std::filesystem;

#define CHECK(condition)                                                                                               \
    do {                                                                                                               \
        if (!(condition)) {                                                                                            \
            std::cerr << "check failed: " #condition << " at line " << __LINE__ << "\n";                               \
            return EXIT_FAILURE;                                                                                       \
        }                                                                                                              \
    } while (false)

namespace {

class TempDirectory {
  public:
    TempDirectory()
        : path_(fs::temp_directory_path() /
                ("minilisysm-process-memory-test-" + std::to_string(::getpid()) + "-" +
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

bool read_byte_with_timeout(int fd, char expected, std::chrono::milliseconds timeout) {
    pollfd descriptor{fd, POLLIN, 0};
    int result = 0;
    do {
        result = ::poll(&descriptor, 1, static_cast<int>(timeout.count()));
    } while (result < 0 && errno == EINTR);
    if (result != 1 || (descriptor.revents & POLLIN) == 0) {
        return false;
    }
    char value = 0;
    ssize_t bytes = 0;
    do {
        bytes = ::read(fd, &value, 1);
    } while (bytes < 0 && errno == EINTR);
    return bytes == 1 && value == expected;
}

bool write_byte(int fd, char value) {
    ssize_t bytes = 0;
    do {
        bytes = ::write(fd, &value, 1);
    } while (bytes < 0 && errno == EINTR);
    return bytes == 1;
}

void touch_pages(std::vector<unsigned char>& allocation) {
    volatile unsigned char* data = allocation.data();
    for (size_t offset = 0; offset < allocation.size(); offset += 4096) {
        data[offset] = 1;
    }
}

class MemoryGrowthHelper {
  public:
    ~MemoryGrowthHelper() {
        stop();
    }

    bool start() {
        int command_pipe[2] = {-1, -1};
        int status_pipe[2] = {-1, -1};
        if (::pipe(command_pipe) != 0 || ::pipe(status_pipe) != 0) {
            close_pair(command_pipe);
            close_pair(status_pipe);
            return false;
        }

        pid_ = ::fork();
        if (pid_ < 0) {
            close_pair(command_pipe);
            close_pair(status_pipe);
            return false;
        }
        if (pid_ == 0) {
            (void)::close(command_pipe[1]);
            (void)::close(status_pipe[0]);
            run_child(command_pipe[0], status_pipe[1]);
        }

        (void)::close(command_pipe[0]);
        (void)::close(status_pipe[1]);
        command_fd_ = command_pipe[1];
        status_fd_ = status_pipe[0];
        name_ = "pmem" + std::to_string(pid_);
        return read_byte_with_timeout(status_fd_, 'R', std::chrono::seconds(2));
    }

    bool grow() {
        return write_byte(command_fd_, 'G') &&
               read_byte_with_timeout(status_fd_, 'G', std::chrono::seconds(2));
    }

    pid_t pid() const {
        return pid_;
    }

    const std::string& name() const {
        return name_;
    }

  private:
    static void close_pair(int (&fds)[2]) {
        for (int& fd : fds) {
            if (fd >= 0) {
                (void)::close(fd);
                fd = -1;
            }
        }
    }

    [[noreturn]] static void run_child(int command_fd, int status_fd) {
        const std::string name = "pmem" + std::to_string(::getpid());
        if (name.size() > 15 || ::prctl(PR_SET_NAME, name.c_str(), 0, 0, 0) != 0) {
            _exit(2);
        }

        std::vector<unsigned char> baseline(1024 * 1024);
        touch_pages(baseline);
        if (!write_byte(status_fd, 'R')) {
            _exit(3);
        }

        char command = 0;
        ssize_t bytes = 0;
        do {
            bytes = ::read(command_fd, &command, 1);
        } while (bytes < 0 && errno == EINTR);
        if (bytes != 1 || command != 'G') {
            _exit(4);
        }

        std::vector<unsigned char> growth(16 * 1024 * 1024);
        touch_pages(growth);
        if (!write_byte(status_fd, 'G')) {
            _exit(5);
        }

        while (::pause() < 0 && errno == EINTR) {
        }
        _exit(0);
    }

    void stop() {
        if (command_fd_ >= 0) {
            (void)::close(command_fd_);
            command_fd_ = -1;
        }
        if (status_fd_ >= 0) {
            (void)::close(status_fd_);
            status_fd_ = -1;
        }
        if (pid_ <= 0) {
            return;
        }
        (void)::kill(pid_, SIGKILL);
        int ignored = 0;
        while (::waitpid(pid_, &ignored, 0) < 0 && errno == EINTR) {
        }
        pid_ = -1;
    }

    pid_t pid_{-1};
    int command_fd_{-1};
    int status_fd_{-1};
    std::string name_;
};

bool has_process_memory_event(const fs::path& root, pid_t pid) {
    std::error_code error;
    if (!fs::exists(root, error) || error) {
        return false;
    }
    const std::string pid_field = "\"pid\":" + std::to_string(pid);
    fs::recursive_directory_iterator current(root, fs::directory_options::skip_permission_denied, error);
    const fs::recursive_directory_iterator end;
    while (!error && current != end) {
        std::error_code file_error;
        const fs::directory_entry& entry = *current;
        if (entry.is_regular_file(file_error) && !file_error && entry.path().extension() == ".jsonl") {
            std::ifstream input(entry.path());
            std::string line;
            while (std::getline(input, line)) {
                if (line.find("\"event_type\":\"whitelisted_process_memory_risk\"") != std::string::npos &&
                    line.find(pid_field) != std::string::npos) {
                    return true;
                }
            }
        }
        current.increment(error);
    }
    return false;
}

} // namespace

int main() {
    (void)std::signal(SIGPIPE, SIG_IGN);

    TempDirectory temp_directory;
    CHECK(temp_directory.created());

    MemoryGrowthHelper helper;
    CHECK(helper.start());

    lisysm::MonitorConfig config;
    config.fast_collect_interval_ms = 50;
    config.low_freq_collect_interval_ms = 10000;
    config.metrics_enable = false;
    config.metrics_scrape_collectors = false;
    config.persistence_enable = true;
    config.cache_path = temp_directory.path().string();
    config.summary_enable = false;
    config.network_sink_enable = false;
    config.memory_rule_enable = false;
    config.self_protection_enable = false;
    config.sched_delay_enable = false;
    config.io_delay_enable = false;
    config.cpu_usage_enable = false;
    config.process_memory_enable = true;
    config.process_memory_growth_warning_mb = 1;
    config.process_memory_growth_critical_mb = 1;
    config.process_memory_growth_recovery_mb = 0;
    config.process_memory_growth_window_sec = 1;
    config.cooldown_sec = 0;
    config.sched_process_whitelist = {helper.name()};

    lisysm::Monitor monitor(config);
    CHECK(monitor.start());
    std::this_thread::sleep_for(std::chrono::milliseconds(1200));
    CHECK(helper.grow());

    bool found_event = false;
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(4);
    while (std::chrono::steady_clock::now() < deadline) {
        if (has_process_memory_event(temp_directory.path(), helper.pid())) {
            found_event = true;
            break;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }
    monitor.stop();
    CHECK(found_event);
    return EXIT_SUCCESS;
}

#endif
