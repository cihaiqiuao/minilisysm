#pragma once

#include "lisysm/config.hpp"
#include "lisysm/event.hpp"

#include <string>

namespace lisysm {

class EventSerializer {
public:
    explicit EventSerializer(const MonitorConfig& config);
    std::string to_json_line(const InternalEvent& event) const;

private:
    const MonitorConfig& config_;
};

} // namespace lisysm
