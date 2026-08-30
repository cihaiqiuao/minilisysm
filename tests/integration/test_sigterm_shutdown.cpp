#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <thread>

#if defined(__linux__)
#include <cerrno>
#include <csignal>
#include <fcntl.h>
#include <sys/wait.h>
#include <unistd.h>
#endif

#if !defined(__linux__)

int main() {
    std::cout << "sigterm shutdown integration test skipped: Linux only\n";
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
                ("minilisysm-sigterm-test-" + std::to_string(::getpid()) + "-" +
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

class ChildProcess {
  public:
    ~ChildProcess() {
        if (pid_ <= 0 || reaped_) {
            return;
        }
        (void)::kill(pid_, SIGKILL);
        int ignored = 0;
        while (::waitpid(pid_, &ignored, 0) < 0 && errno == EINTR) {
        }
    }

    void set_pid(pid_t pid) {
        pid_ = pid;
    }

    pid_t pid() const {
        return pid_;
    }

    bool poll(int* status) {
        int current_status = 0;
        pid_t result = 0;
        do {
            result = ::waitpid(pid_, &current_status, WNOHANG);
        } while (result < 0 && errno == EINTR);
        if (result != pid_) {
            return false;
        }
        reaped_ = true;
        if (status) {
            *status = current_status;
        }
        return true;
    }

  private:
    pid_t pid_{-1};
    bool reaped_{false};
};

bool write_config(const fs::path& path, const fs::path& cache_path, const fs::path& agent_log_path) {
    std::ofstream output(path, std::ios::out | std::ios::trunc);
    output << "[linux_stability_monitor]\n"
           << "enable=true\n"
           << "fast_collect_interval_ms=1000\n"
           << "low_freq_collect_interval_ms=10000\n\n"
           << "[metrics]\n"
           << "enable=false\n\n"
           << "[agent_log]\n"
           << "enable=true\n"
           << "console=false\n"
           << "path=" << agent_log_path.string() << "\n\n"
           << "[memory_rule]\n"
           << "enable=false\n\n"
           << "[self_protection]\n"
           << "enable=false\n\n"
           << "[sched_delay_rule]\n"
           << "enable=false\n\n"
           << "[io_delay_rule]\n"
           << "enable=false\n\n"
           << "[cpu_usage_rule]\n"
           << "enable=false\n\n"
           << "[persistence]\n"
           << "enable=true\n"
           << "cache_path=" << cache_path.string() << "\n"
           << "summary_enable=false\n\n"
           << "[network_sink]\n"
           << "enable=false\n";
    output.close();
    return output.good();
}

bool has_jsonl(const fs::path& root, bool require_data) {
    std::error_code error;
    if (!fs::exists(root, error) || error) {
        return false;
    }
    fs::recursive_directory_iterator current(root, fs::directory_options::skip_permission_denied, error);
    const fs::recursive_directory_iterator end;
    while (!error && current != end) {
        std::error_code file_error;
        const fs::directory_entry& entry = *current;
        if (entry.is_regular_file(file_error) && !file_error && entry.path().extension() == ".jsonl") {
            if (!require_data || (entry.file_size(file_error) > 0 && !file_error)) {
                return true;
            }
        }
        current.increment(error);
    }
    return false;
}

std::string read_text(const fs::path& path) {
    std::ifstream input(path);
    std::ostringstream output;
    output << input.rdbuf();
    return output.str();
}

size_t count_occurrences(const std::string& text, const std::string& needle) {
    size_t count = 0;
    size_t offset = 0;
    while ((offset = text.find(needle, offset)) != std::string::npos) {
        ++count;
        offset += needle.size();
    }
    return count;
}

std::string describe_status(int status) {
    if (WIFEXITED(status)) {
        return "exit=" + std::to_string(WEXITSTATUS(status));
    }
    if (WIFSIGNALED(status)) {
        return "signal=" + std::to_string(WTERMSIG(status));
    }
    return "status=" + std::to_string(status);
}

} // namespace

int main(int argc, char** argv) {
    CHECK(argc == 2);

    std::error_code path_error;
    const fs::path agent_path = fs::absolute(argv[1], path_error);
    CHECK(!path_error);
    CHECK(fs::is_regular_file(agent_path));

    TempDirectory temp_directory;
    CHECK(temp_directory.created());
    const fs::path config_path = temp_directory.path() / "lisysm_monitor.ini";
    const fs::path cache_path = temp_directory.path() / "events";
    const fs::path agent_log_path = temp_directory.path() / "agent.log";
    const fs::path console_log_path = temp_directory.path() / "console.log";
    CHECK(write_config(config_path, cache_path, agent_log_path));

    ChildProcess child;
    const pid_t pid = ::fork();
    CHECK(pid >= 0);
    if (pid == 0) {
        const int console_fd = ::open(console_log_path.c_str(), O_CREAT | O_WRONLY | O_TRUNC, 0600);
        if (console_fd >= 0) {
            (void)::dup2(console_fd, STDOUT_FILENO);
            (void)::dup2(console_fd, STDERR_FILENO);
            (void)::close(console_fd);
        }
        if (::chdir(temp_directory.path().c_str()) != 0) {
            _exit(126);
        }
        ::execl(agent_path.c_str(), agent_path.c_str(), config_path.c_str(), static_cast<char*>(nullptr));
        _exit(127);
    }
    child.set_pid(pid);

    int child_status = 0;
    bool started = false;
    const auto startup_deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
    while (std::chrono::steady_clock::now() < startup_deadline) {
        if (child.poll(&child_status)) {
            std::cerr << "agent exited before startup was observable: " << describe_status(child_status) << "\n"
                      << read_text(console_log_path);
            return EXIT_FAILURE;
        }
        if (has_jsonl(cache_path, false)) {
            started = true;
            break;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }
    if (!started) {
        std::cerr << "agent startup was not observable\n"
                  << "console log:\n"
                  << read_text(console_log_path) << "\nagent log:\n"
                  << read_text(agent_log_path) << "\ncreated paths:\n";
        std::error_code list_error;
        fs::recursive_directory_iterator current(temp_directory.path(), list_error);
        const fs::recursive_directory_iterator end;
        while (!list_error && current != end) {
            std::cerr << current->path() << "\n";
            current.increment(list_error);
        }
        return EXIT_FAILURE;
    }
    CHECK(::kill(child.pid(), SIGTERM) == 0);

    bool exited = false;
    const auto shutdown_deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
    while (std::chrono::steady_clock::now() < shutdown_deadline) {
        if (child.poll(&child_status)) {
            exited = true;
            break;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }
    CHECK(exited);
    if (!WIFEXITED(child_status) || WEXITSTATUS(child_status) != 0) {
        std::cerr << "agent shutdown failed: " << describe_status(child_status) << "\n"
                  << read_text(console_log_path);
        return EXIT_FAILURE;
    }

    CHECK(has_jsonl(cache_path, true));
    const std::string agent_log = read_text(agent_log_path);
    CHECK(count_occurrences(agent_log, "monitor stopped") == 1);
    return EXIT_SUCCESS;
}

#endif
