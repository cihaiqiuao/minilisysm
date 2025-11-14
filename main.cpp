#include "cpu_monitor.h"

int main(int argc, char* argv[]) {
    // 1. 填默认配置
    CpuMonitor::Options opt;

    // 2. 简单解析参数
    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--json") {
            opt.json_mode = true;
        }
        else if (arg == "i" || arg == "--interval") {
            if (i + 1 >= argc) {
                std::cerr << "miss value for" << arg << "\n";
            }
            opt.interval_ms = std::stoi(argv[i + 1]);
            if (opt.interval_ms <= 0) {
                std::cerr << "interval must be >0ms\n";
                return 1;
            }
        }
        else if (arg == "-n" || arg == "--count") {
            if (i + 1 >= argc) {
                std::cerr << "Missing value for " << arg << "\n";
                return 1;
            }
            opt.max_samples = std::stoi(argv[++i]);
            if (opt.max_samples <= 0) {
                std::cerr << "count must be > 0\n";
                return 1;
            }
        }
        else if (arg == "-h" || arg == "--help") {
            std::cout << "Usage: " << argv[0] << " [options]\n"
                << "Options:\n"
                << "  --json        Output JSON lines (ts, cpu, usage)\n"
                << "  -h, --help    Show this help message\n";
            return 0;
        }
        else {
            std::cerr << "Unknown option: " << arg << "\n";
            std::cout << "Use -h or --help for usage.\n";
            return 1;
        }
    }

    // 3. 创建监控对象
    CpuMonitor monitor(opt);

    // 4. 把实例挂到静态指针上，供信号处理函数调用
    CpuMonitor::set_instance(&monitor);

    // 5. 注册 SIGINT（Ctrl+C）
    std::signal(SIGINT, &CpuMonitor::handle_sigint);

    // 6. 跑主循环
    return monitor.run();
}
