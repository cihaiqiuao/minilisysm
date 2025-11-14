#pragma once

#include <chrono>
#include <csignal>
#include <cstring>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <string>
#include <thread>
#include <vector>
#include <algorithm>
#include <ctime>

// CPU 各字段
struct CpuTimes {
    unsigned long long user = 0;
    unsigned long long nice = 0;
    unsigned long long system = 0;
    unsigned long long idle = 0;
    unsigned long long iowait = 0;
    unsigned long long irq = 0;
    unsigned long long softirq = 0;
    unsigned long long steal = 0;
    unsigned long long guest = 0;
    unsigned long long guest_nice = 0;
};

// 一次快照：多路 cpu + 各自的时间
struct Snapshot {
    std::vector<std::pair<std::string, CpuTimes>> cpus;
};

class CpuMonitor {
public:
    // 运行配置
    struct Options {
        bool json_mode = false;  // 是否输出 JSON
        int  interval_ms = 1000;   // 采样间隔（毫秒）
        int max_samples = -1;
    };

    explicit CpuMonitor(const Options& opt);

    // 主循环：开始监控，直到收到 SIGINT 或出错
    int run();

    // 用于 std::signal 注册的信号处理函数
    static void handle_sigint(int sig);

    // 在 main 里把当前实例挂到静态指针上
    static void set_instance(CpuMonitor* inst);

private:
    Options opt_;
    bool stop_ = false;
    Snapshot prev_;             // 上一次采样
    static CpuMonitor* instance_; // 静态实例指针，给信号处理用

    // 内部工具函数
    std::string current_timestamp() const;
    const char* color_for_usage(double usage) const;
    void print_header(std::size_t header_every, std::size_t line_count) const;

    bool starts_with(const std::string& s, const char* prefix) const;
    CpuTimes parse_cpu_fields(std::istringstream& iss) const;
    Snapshot read_proc_stat() const;
    double usage_between(const CpuTimes& a, const CpuTimes& b) const;

    void request_stop();  // 被信号处理函数调用
};
