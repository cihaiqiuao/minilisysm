#include "minilisysm/collectors/meminfo_collector.hpp"

#include <cstdlib>
#include <fstream>
#include <iostream>
#include <string>

#define CHECK(condition)                                                                                               \
    do {                                                                                                               \
        if (!(condition)) {                                                                                            \
            std::cerr << "check failed: " #condition << " at line " << __LINE__ << "\n";                               \
            return EXIT_FAILURE;                                                                                       \
        }                                                                                                              \
    } while (false)

void write_file(const std::string& path, const std::string& content) {
    std::ofstream os(path);
    os << content;
}

int main() {
    const std::string test_file = "/tmp/mock_meminfo";

    // Test 1: Valid data
    write_file(test_file,
               "MemTotal:       16301328 kB\n"
               "MemFree:          123456 kB\n"
               "MemAvailable:    8192000 kB\n"
               "SwapFree:        4194304 kB\n"
               "SReclaimable:     512000 kB\n"
               "SUnreclaim:        64000 kB\n");

    lisysm::MeminfoCollector valid_collector(test_file);
    lisysm::MeminfoSample sample = valid_collector.collect();

    CHECK(sample.valid);
    CHECK(sample.mem_total_kb == 16301328);
    CHECK(sample.mem_available_kb == 8192000);
    CHECK(sample.swap_free_kb == 4194304);
    CHECK(sample.sreclaimable_kb == 512000);
    CHECK(sample.sunreclaim_kb == 64000);

    // Test 2: Missing required fields
    write_file(test_file, "MemTotal:       16301328 kB\n");
    lisysm::MeminfoSample sample_invalid = valid_collector.collect();
    CHECK(!sample_invalid.valid);
    CHECK(sample_invalid.mem_total_kb == 16301328);
    CHECK(sample_invalid.mem_available_kb == 0);

    // Test 3: Empty file
    write_file(test_file, "");
    lisysm::MeminfoSample sample_empty = valid_collector.collect();
    CHECK(!sample_empty.valid);

    // Test 4: File not found
    lisysm::MeminfoCollector not_found_collector("/tmp/non_existent_mock_meminfo");
    lisysm::MeminfoSample sample_not_found = not_found_collector.collect();
    CHECK(!sample_not_found.valid);

    // Cleanup
    std::remove(test_file.c_str());

    return 0;
}
