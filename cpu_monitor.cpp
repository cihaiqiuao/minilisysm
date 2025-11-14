#include "cpu_monitor.h"

CpuMonitor* CpuMonitor::instance_ = nullptr;

CpuMonitor::CpuMonitor(const Options& opt)
    : opt_(opt) {
}

// 静态：给 std::signal 用
void CpuMonitor::handle_sigint(int /*sig*/) {
    if (instance_) {
        instance_->request_stop();
    }
}

// 在 main 里调用，把当前对象挂上来
void CpuMonitor::set_instance(CpuMonitor* inst) {
    instance_ = inst;
}

void CpuMonitor::request_stop() {
    stop_ = true;
}

// 获取当前时间戳：YYYY-MM-DD HH:MM:SS
std::string CpuMonitor::current_timestamp() const {
    using clock = std::chrono::system_clock;
    auto now = clock::now();
    std::time_t t = clock::to_time_t(now);

    std::tm tm_buf{};
    localtime_r(&t, &tm_buf);

    char buf[32];
    std::strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", &tm_buf);
    return std::string(buf);
}

// 根据 usage 选择颜色
const char* CpuMonitor::color_for_usage(double usage) const {
    if (usage < 30.0) {
        return "\033[32m"; // 绿
    }
    else if (usage < 70.0) {
        return "\033[33m"; // 黄
    }
    else {
        return "\033[31m"; // 红
    }
}

// 打印表头（仅在非 JSON 模式下）
void CpuMonitor::print_header(std::size_t header_every,
    std::size_t line_count) const {
    if (opt_.json_mode) return;
    if (line_count % header_every != 0) return;

    std::cout << "\nCPU Usage (%) - sample interval "
        << opt_.interval_ms << " ms\n";
    std::cout << std::left
        << std::setw(19) << "Time"
        << std::setw(8) << "CPU"
        << std::right << std::setw(8) << "Usage%\n";
    std::cout << std::string(19 + 8 + 8, '-') << "\n";
}

// 以下都是内部解析和计算函数

bool CpuMonitor::starts_with(const std::string& s,
    const char* prefix) const {
    return s.compare(0, std::strlen(prefix), prefix) == 0;
}

CpuTimes CpuMonitor::parse_cpu_fields(std::istringstream& iss) const {
    CpuTimes t{};
    iss >> t.user >> t.nice >> t.system >> t.idle >> t.iowait
        >> t.irq >> t.softirq >> t.steal >> t.guest >> t.guest_nice;
    return t;
}

Snapshot CpuMonitor::read_proc_stat() const {
    Snapshot snap;
    std::ifstream in("/proc/stat");
    std::string line;

    if (!in.is_open()) {
        throw std::runtime_error("Failed to open /proc/stat");
    }

    while (std::getline(in, line)) {
        if (!starts_with(line, "cpu")) break;
        std::istringstream iss(line);
        std::string name;
        iss >> name;
        CpuTimes t = parse_cpu_fields(iss);
        snap.cpus.emplace_back(name, t);
    }
    return snap;
}

double CpuMonitor::usage_between(const CpuTimes& a,
    const CpuTimes& b) const {
    auto idle_a = a.idle + a.iowait;
    auto idle_b = b.idle + b.iowait;

    auto non_a = a.user + a.nice + a.system + a.irq + a.softirq + a.steal;
    auto non_b = b.user + b.nice + b.system + b.irq + b.softirq + b.steal;

    auto total_a = idle_a + non_a;
    auto total_b = idle_b + non_b;

    const double totald = static_cast<double>(total_b - total_a);
    const double idled = static_cast<double>(idle_b - idle_a);

    if (totald <= 0.0) return 0.0;
    return (totald - idled) / totald * 100.0;
}

// 主监控循环
int CpuMonitor::run() {
    std::cout.setf(std::ios::fixed);
    std::cout << std::setprecision(1);

    try {
        prev_ = read_proc_stat();
    }
    catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << '\n';
        return 1;
    }

    std::size_t header_every = 20;
    std::size_t line_count = 0;
    std::size_t samples = 0;

    print_header(header_every, line_count);

    while (!stop_) {
        if (opt_.max_samples > 0 && samples >= static_cast<std::size_t>(opt_.max_samples)) { break; }

        std::this_thread::sleep_for(
            std::chrono::milliseconds(opt_.interval_ms));

        Snapshot curr;
        try {
            curr = read_proc_stat();
        }
        catch (const std::exception& e) {
            std::cerr << "Error: " << e.what() << '\n';
            break;
        }

        const std::size_t n = std::min(prev_.cpus.size(), curr.cpus.size());
        const std::string ts = current_timestamp();

        print_header(header_every, line_count);

        for (std::size_t i = 0; i < n; ++i) {
            const auto& name = curr.cpus[i].first;   // "cpu" / "cpu0" ...
            const auto& a = prev_.cpus[i].second;
            const auto& b = curr.cpus[i].second;
            const double u = usage_between(a, b);

            if (opt_.json_mode) {
                // JSON 一行
                std::cout << "{\"ts\":\"" << ts
                    << "\",\"cpu\":\"" << name
                    << "\",\"usage\":" << u << "}\n";
            }
            else {
                const char* color = color_for_usage(u);
                const char* reset = "\033[0m";

                std::cout << std::left
                    << std::setw(19) << ts << ' '
                    << std::setw(8) << name
                    << std::right;

                std::cout << color
                    << std::setw(8) << u
                    << reset << "\n";
            }
        }

        ++line_count;
        ++samples;
        prev_ = std::move(curr);
    }

    if (!opt_.json_mode) {
        std::cout << "Bye.\n";
    }
    return 0;
}
