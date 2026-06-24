#pragma once

#include <array>
#include <cstddef>
#include <string>
#include <string_view>

namespace lisysm {

class LinuxProcReader {
public:
    explicit LinuxProcReader(std::string path);
    ~LinuxProcReader();

    LinuxProcReader(const LinuxProcReader&) = delete;
    LinuxProcReader& operator=(const LinuxProcReader&) = delete;

    bool read(std::string_view* view);
    const std::string& path() const { return path_; }

private:
    bool open_fd();
    void close_fd();

    std::string path_;
    int fd_{-1};
    std::array<char, 8192> buffer_{};
    size_t size_{0};
};

} // namespace lisysm
