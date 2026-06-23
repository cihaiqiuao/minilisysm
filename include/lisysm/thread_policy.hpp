#pragma once

#include <string>

namespace lisysm {

bool set_current_thread_affinity(int cpu, std::string* error);
bool set_current_thread_nice(int nice_value, std::string* error);

} // namespace lisysm
