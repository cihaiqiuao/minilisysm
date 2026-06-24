#include "lisysm/collectors/linux_proc_reader.hpp"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <utility>

#if defined(__linux__)
#include <fcntl.h>
#include <unistd.h>
#endif

namespace lisysm {

LinuxProcReader::LinuxProcReader(std::string path) : path_(std::move(path)) {}

LinuxProcReader::~LinuxProcReader()
{
    close_fd();
}

bool LinuxProcReader::read(std::string_view* view)
{
#if defined(__linux__)
    if (fd_ < 0 && !open_fd()) {
        return false;
    }
    if (::lseek(fd_, 0, SEEK_SET) < 0) {
        close_fd();
        if (!open_fd()) {
            return false;
        }
    }
    const ssize_t n = ::read(fd_, buffer_.data(), buffer_.size() - 1);
    if (n <= 0) {
        close_fd();
        return false;
    }
    size_ = static_cast<size_t>(n);
    buffer_[size_] = '\0';
    *view = std::string_view(buffer_.data(), size_);
    return true;
#else
    std::ifstream stream(path_, std::ios::binary);
    if (!stream) {
        return false;
    }
    stream.read(buffer_.data(), static_cast<std::streamsize>(buffer_.size() - 1));
    size_ = static_cast<size_t>(stream.gcount());
    buffer_[size_] = '\0';
    *view = std::string_view(buffer_.data(), size_);
    return size_ > 0;
#endif
}

bool LinuxProcReader::open_fd()
{
#if defined(__linux__)
    fd_ = ::open(path_.c_str(), O_RDONLY | O_CLOEXEC);
    return fd_ >= 0;
#else
    return true;
#endif
}

void LinuxProcReader::close_fd()
{
#if defined(__linux__)
    if (fd_ >= 0) {
        ::close(fd_);
        fd_ = -1;
    }
#endif
}

} // namespace lisysm
