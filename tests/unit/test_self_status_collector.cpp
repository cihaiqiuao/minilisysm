#include "minilisysm/collectors/self_status_collector.hpp"

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
    const std::string test_file = "/tmp/mock_self_status";

    // Test 1: Valid data
    write_file(test_file,
               "Name:\tminilisysm\n"
               "Umask:\t0022\n"
               "State:\tS (sleeping)\n"
               "Tgid:\t12345\n"
               "Ngid:\t0\n"
               "Pid:\t12345\n"
               "PPid:\t1\n"
               "TracerPid:\t0\n"
               "Uid:\t1000\t1000\t1000\t1000\n"
               "Gid:\t1000\t1000\t1000\t1000\n"
               "FDSize:\t256\n"
               "Groups:\t4 20 24 27 30 46 116 118 1000 \n"
               "VmPeak:\t  123456 kB\n"
               "VmSize:\t   98765 kB\n"
               "VmLck:\t       0 kB\n"
               "VmPin:\t       0 kB\n"
               "VmHWM:\t   10240 kB\n"
               "VmRSS:\t    8192 kB\n"
               "RssAnon:\t    4096 kB\n"
               "RssFile:\t    4096 kB\n"
               "RssShmem:\t       0 kB\n"
               "VmData:\t    1024 kB\n"
               "VmStk:\t     132 kB\n"
               "VmExe:\t    3456 kB\n"
               "VmLib:\t    3000 kB\n"
               "VmPTE:\t      40 kB\n"
               "VmSwap:\t       0 kB\n");

    lisysm::SelfStatusCollector valid_collector(test_file);
    lisysm::SelfStatusSample sample = valid_collector.collect();

    CHECK(sample.valid);
    CHECK(sample.vm_rss_kb == 8192);

    // Test 2: Missing VmRSS
    write_file(test_file, "Name:\tminilisysm\nPid:\t12345\n");
    lisysm::SelfStatusSample sample_invalid = valid_collector.collect();
    CHECK(!sample_invalid.valid);
    CHECK(sample_invalid.vm_rss_kb == 0);

    // Test 3: Empty file
    write_file(test_file, "");
    lisysm::SelfStatusSample sample_empty = valid_collector.collect();
    CHECK(!sample_empty.valid);

    // Test 4: File not found
    lisysm::SelfStatusCollector not_found_collector("/tmp/non_existent_mock_self_status");
    lisysm::SelfStatusSample sample_not_found = not_found_collector.collect();
    CHECK(!sample_not_found.valid);

    // Cleanup
    std::remove(test_file.c_str());

    return 0;
}
