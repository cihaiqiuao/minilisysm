#pragma once

#include "minilisysm/core/config.hpp"
#include "minilisysm/core/event.hpp"

#include <string>

namespace lisysm {

class EventSerializer {
  public:
    explicit EventSerializer(const MonitorConfig& config);
    std::string to_json_line(const InternalEvent& event) const;
    void to_json_line(const InternalEvent& event, std::string& output) const;

  private:
    const MonitorConfig& config_;
};

} // namespace lisysm
