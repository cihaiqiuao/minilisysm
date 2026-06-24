#include "lisysm/collectors/meminfo_collector.hpp"

#include <charconv>
#include <cstring>
#include <system_error>

namespace lisysm {
namespace {

bool parse_kb_line(std::string_view line, const char* key, uint64_t* value)
{
    const size_t key_len = std::strlen(key);
    if (line.size() <= key_len || line.substr(0, key_len) != key) {
        return false;
    }
    const char* begin = line.data() + key_len;
    const char* end = line.data() + line.size();
    while (begin < end && (*begin == ':' || *begin == ' ' || *begin == '\t')) {
        ++begin;
    }
    uint64_t parsed = 0;
    if (std::from_chars(begin, end, parsed).ec != std::errc{}) {
        return false;
    }
    *value = parsed;
    return true;
}

} // namespace

MeminfoCollector::MeminfoCollector() : reader_("/proc/meminfo") {}

MeminfoSample MeminfoCollector::collect()
{
    MeminfoSample sample;
    std::string_view content;
    if (!reader_.read(&content)) {
        return sample;
    }

    while (!content.empty()) {
        const size_t pos = content.find('\n');
        const std::string_view line = pos == std::string_view::npos ? content : content.substr(0, pos);
        parse_kb_line(line, "MemTotal", &sample.mem_total_kb) ||
            parse_kb_line(line, "MemAvailable", &sample.mem_available_kb) ||
            parse_kb_line(line, "SwapFree", &sample.swap_free_kb) ||
            parse_kb_line(line, "SReclaimable", &sample.sreclaimable_kb) ||
            parse_kb_line(line, "SUnreclaim", &sample.sunreclaim_kb);
        if (pos == std::string_view::npos) {
            break;
        }
        content.remove_prefix(pos + 1);
    }
    sample.valid = sample.mem_total_kb > 0 && sample.mem_available_kb > 0;
    return sample;
}

} // namespace lisysm
