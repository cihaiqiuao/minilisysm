#include "minilisysm/runtime/metrics_server.hpp"

#include <spdlog/spdlog.h>

#include <chrono>
#include <cstring>
#include <sstream>
#include <string>
#include <thread>
#include <utility>

#if defined(__linux__)
#include <arpa/inet.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <poll.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>
#endif

namespace lisysm {
namespace {

std::string http_response(int status, const char* status_text, const char* content_type, const std::string& body) {
    std::ostringstream output;
    output << "HTTP/1.1 " << status << ' ' << status_text << "\r\n"
           << "Content-Type: " << content_type << "\r\n"
           << "Content-Length: " << body.size() << "\r\n"
           << "Connection: close\r\n\r\n"
           << body;
    return output.str();
}

std::string status_page_html() {
    return R"HTML(<!doctype html>
<html lang="zh-CN">
<head>
  <meta charset="utf-8">
  <meta name="viewport" content="width=device-width, initial-scale=1">
  <title>MiniLisySM 状态总览</title>
  <style>
    :root {
      color-scheme: light;
      font-family: Inter, "Segoe UI", "Microsoft YaHei", system-ui, sans-serif;
      --bg: #eef3f8;
      --panel: #ffffff;
      --ink: #182233;
      --muted: #627086;
      --line: #d8e1ee;
      --green: #14804a;
      --red: #bd2d20;
      --blue: #2867c7;
      --amber: #a45f08;
    }
    * { box-sizing: border-box; }
    body { margin: 0; background: var(--bg); color: var(--ink); }
    main { max-width: 1180px; margin: 0 auto; padding: 22px; }
    header {
      min-height: 168px;
      display: flex;
      align-items: flex-end;
      justify-content: space-between;
      gap: 20px;
      padding: 26px;
      margin-bottom: 16px;
      border: 1px solid var(--line);
      border-radius: 8px;
      background:
        linear-gradient(135deg, rgba(40, 103, 199, 0.16), rgba(20, 128, 74, 0.10)),
        var(--panel);
    }
    h1 { margin: 0; font-size: 34px; line-height: 1.15; }
    h2 { margin: 24px 0 12px; font-size: 19px; }
    .subtitle { margin-top: 8px; color: var(--muted); font-size: 15px; }
    .top-actions { display: flex; align-items: center; gap: 10px; flex-wrap: wrap; justify-content: flex-end; }
    .chip, .link-button {
      min-height: 34px;
      display: inline-flex;
      align-items: center;
      gap: 8px;
      border-radius: 999px;
      padding: 7px 12px;
      border: 1px solid var(--line);
      background: rgba(255, 255, 255, 0.76);
      color: var(--ink);
      text-decoration: none;
      font-size: 14px;
    }
    .dot { width: 9px; height: 9px; border-radius: 50%; background: var(--amber); }
    .dot.ok { background: var(--green); }
    .dot.bad { background: var(--red); }
    .grid { display: grid; grid-template-columns: repeat(4, minmax(0, 1fr)); gap: 12px; }
    .card {
      background: var(--panel);
      border: 1px solid var(--line);
      border-radius: 8px;
      padding: 15px;
      min-height: 116px;
      box-shadow: 0 10px 26px rgba(23, 32, 51, 0.05);
    }
    .card.wide { grid-column: span 2; }
    .label { color: var(--muted); font-size: 13px; }
    .value { font-size: 28px; font-weight: 700; margin-top: 8px; overflow-wrap: anywhere; }
    .hint { color: var(--muted); font-size: 13px; margin-top: 8px; }
    .ok-text { color: var(--green); }
    .bad-text { color: var(--red); }
    .warn-text { color: var(--amber); }
    .bar { height: 10px; border-radius: 999px; background: #e7edf5; overflow: hidden; margin-top: 12px; }
    .bar > span { display: block; height: 100%; width: 0; background: var(--blue); border-radius: inherit; }
    .split { display: grid; grid-template-columns: 1fr 1fr; gap: 12px; }
    .table { width: 100%; border-collapse: collapse; font-size: 14px; }
    .table th, .table td { text-align: left; padding: 10px 8px; border-bottom: 1px solid var(--line); }
    .table th { color: var(--muted); font-weight: 600; }
    details {
      background: var(--panel);
      border: 1px solid var(--line);
      border-radius: 8px;
      padding: 14px;
      margin-top: 12px;
    }
    summary { cursor: pointer; font-weight: 650; }
    pre {
      margin: 14px 0 0;
      background: #111827;
      color: #d6e2ff;
      border-radius: 8px;
      padding: 14px;
      overflow: auto;
      max-height: 40vh;
      font-size: 12px;
    }
    @media (max-width: 820px) {
      main { padding: 14px; }
      header { align-items: flex-start; flex-direction: column; min-height: 0; padding: 18px; }
      h1 { font-size: 28px; }
      .grid, .split { grid-template-columns: 1fr; }
      .card.wide { grid-column: auto; }
    }
  </style>
</head>
<body>
<main>
  <header>
    <div>
      <h1>MiniLisySM 状态总览</h1>
      <div class="subtitle">Linux 稳定性监控实时面板，数据来自本机 /metrics。</div>
    </div>
    <div class="top-actions">
      <span class="chip"><span class="dot" id="live-dot"></span><span id="live-text">等待数据</span></span>
      <span class="chip" id="updated">准备刷新</span>
      <a class="link-button" href="/metrics">查看原始指标</a>
    </div>
  </header>

  <section class="grid" id="cards"></section>

  <section class="split">
    <article class="card wide">
      <h2>队列压力</h2>
      <div id="queues"></div>
    </article>
    <article class="card wide">
      <h2>Collector 健康</h2>
      <table class="table">
        <thead><tr><th>采集器</th><th>失败次数</th><th>状态</th></tr></thead>
        <tbody id="collectors"></tbody>
      </table>
    </article>
  </section>

  <article class="card">
    <h2>硬件健康</h2>
    <table class="table">
      <thead><tr><th>类别</th><th>对象</th><th>指标</th><th>状态</th></tr></thead>
      <tbody id="hardware"></tbody>
    </table>
  </article>

  <details>
    <summary>原始 Metrics 预览</summary>
    <pre id="raw">正在加载...</pre>
  </details>
</main>
<script>
const cardDefs = [
  { name: "minilisysm_up", label: "Agent 状态", format: v => v === 1 ? "运行中" : "异常", hint: "进程与监控主循环" },
  { name: "minilisysm_cpu_usage_percent", label: "CPU 使用率", selector: "cpu=\"total\"", format: v => `${format(v, 1)}%`, hint: "系统总 CPU" },
  { name: "minilisysm_system_memory_available_bytes", label: "可用内存", format: bytes, hint: "MemAvailable" },
  { name: "minilisysm_monitor_rss_bytes", label: "监控自身 RSS", format: bytes, hint: "minilisysm 进程内存" },
  { name: "minilisysm_events_published_total", label: "事件总数", format: v => format(v, 0), hint: "已发布内部事件" },
  { name: "minilisysm_battery_health_percent", label: "电池健康", format: minValue, hint: "满充容量 / 设计容量" },
  { name: "minilisysm_storage_lifetime_used_percent", label: "存储寿命已用", format: maxPercent, hint: "eMMC/UFS/NAND 寿命估计" },
  { name: "minilisysm_memory_ecc_uncorrected_total", label: "ECC 未纠错", format: v => format(v, 0), hint: "内存不可纠正错误" }
];

function bytes(v) {
  if (!Number.isFinite(v)) return "暂无";
  const units = ["B", "KiB", "MiB", "GiB"];
  let value = v;
  let unit = 0;
  while (value >= 1024 && unit < units.length - 1) {
    value /= 1024;
    unit++;
  }
  return `${format(value, unit === 0 ? 0 : 1)} ${units[unit]}`;
}

function format(v, digits) {
  return Number.isFinite(v) ? v.toFixed(digits) : "暂无";
}

function parseMetrics(text) {
  const metrics = new Map();
  for (const line of text.split("\n")) {
    if (!line || line[0] === "#") continue;
    const parts = line.trim().split(/\s+/);
    if (parts.length < 2) continue;
    const value = Number(parts[1]);
    if (!Number.isFinite(value)) continue;
    const name = parts[0].split("{")[0];
    if (!metrics.has(name)) metrics.set(name, []);
    metrics.get(name).push({ sample: parts[0], value });
  }
  return metrics;
}

function pick(metrics, name, label) {
  const samples = metrics.get(name) || [];
  const found = label ? samples.find(item => item.sample.includes(label)) : samples[0];
  return found ? found.value : NaN;
}

function sum(metrics, name) {
  return (metrics.get(name) || []).reduce((total, item) => total + item.value, 0);
}

function minValue(metrics, name) {
  const values = (metrics.get(name) || []).map(item => item.value).filter(Number.isFinite);
  if (values.length === 0) return NaN;
  return `${Math.min(...values).toFixed(1)}%`;
}

function maxPercent(metrics, name) {
  const values = (metrics.get(name) || []).map(item => item.value).filter(Number.isFinite);
  if (values.length === 0) return "暂无";
  return `${Math.max(...values).toFixed(1)}%`;
}

function pct(depth, capacity) {
  if (!Number.isFinite(depth) || !Number.isFinite(capacity) || capacity <= 0) return 0;
  return Math.max(0, Math.min(100, (depth / capacity) * 100));
}

function statusClass(value, warnAt = 1) {
  return Number.isFinite(value) && value >= warnAt ? "warn-text" : "ok-text";
}

function renderCards(metrics) {
  const cards = cardDefs.map(def => {
    const aggregate = def.format === sum || def.format === minValue || def.format === maxPercent;
    const numeric = aggregate ? def.format(metrics, def.name) : pick(metrics, def.name, def.selector);
    const value = def.format === sum ? format(numeric, 0) : aggregate ? numeric : def.format(numeric);
    const warnValue = typeof numeric === "number" ? numeric : 0;
    const cls = def.name === "minilisysm_up" && warnValue !== 1 ? "bad-text" : statusClass(warnValue);
    return `<article class="card"><div class="label">${def.label}</div><div class="value ${cls}">${value}</div><div class="hint">${def.hint}</div></article>`;
  });
  document.getElementById("cards").innerHTML = cards.join("");
}

function renderQueues(metrics) {
  const rows = ["source", "sink"].map(queue => {
    const depth = pick(metrics, "minilisysm_queue_depth", `queue="${queue}"`);
    const capacity = pick(metrics, "minilisysm_queue_capacity", `queue="${queue}"`);
    const dropped = pick(metrics, "minilisysm_queue_dropped_total", `queue="${queue}"`);
    const used = pct(depth, capacity);
    const name = queue === "source" ? "采集队列" : "下沉队列";
    return `<div class="queue-row">
      <div class="label">${name}</div>
      <div class="value">${format(depth, 0)} / ${format(capacity, 0)}</div>
      <div class="bar"><span style="width:${used.toFixed(1)}%"></span></div>
      <div class="hint">占用 ${used.toFixed(1)}%，累计丢弃 ${format(dropped, 0)}</div>
    </div>`;
  });
  document.getElementById("queues").innerHTML = rows.join("");
}

function renderCollectors(metrics) {
  const samples = metrics.get("minilisysm_collector_failures_total") || [];
  const rows = samples.map(item => {
    const match = item.sample.match(/collector="([^"]+)"/);
    const name = match ? match[1] : item.sample;
    const ok = item.value === 0;
    return `<tr><td>${name}</td><td>${format(item.value, 0)}</td><td class="${ok ? "ok-text" : "bad-text"}">${ok ? "正常" : "需关注"}</td></tr>`;
  });
  document.getElementById("collectors").innerHTML = rows.length ? rows.join("") : `<tr><td colspan="3">暂无 collector 指标</td></tr>`;
}

function labelValue(sample, key) {
  const match = sample.match(new RegExp(`${key}="([^"]+)"`));
  return match ? match[1] : "-";
}

function renderHardware(metrics) {
  const rows = [];
  for (const item of metrics.get("minilisysm_battery_capacity_percent") || []) {
    rows.push(`<tr><td>电池</td><td>${labelValue(item.sample, "battery")}</td><td>当前电量 ${format(item.value, 0)}%</td><td class="ok-text">可读</td></tr>`);
  }
  for (const item of metrics.get("minilisysm_battery_health_percent") || []) {
    const cls = item.value < 70 ? "bad-text" : item.value < 85 ? "warn-text" : "ok-text";
    rows.push(`<tr><td>电池</td><td>${labelValue(item.sample, "battery")}</td><td>健康度 ${format(item.value, 1)}%</td><td class="${cls}">${item.value < 85 ? "需关注" : "正常"}</td></tr>`);
  }
  for (const item of metrics.get("minilisysm_battery_cycle_count") || []) {
    rows.push(`<tr><td>电池</td><td>${labelValue(item.sample, "battery")}</td><td>循环 ${format(item.value, 0)} 次</td><td class="ok-text">可读</td></tr>`);
  }
  for (const item of metrics.get("minilisysm_storage_lifetime_used_percent") || []) {
    const cls = item.value >= 90 ? "bad-text" : item.value >= 70 ? "warn-text" : "ok-text";
    rows.push(`<tr><td>存储</td><td>${labelValue(item.sample, "device")}</td><td>寿命已用 ${format(item.value, 1)}%</td><td class="${cls}">${item.value >= 70 ? "需关注" : "正常"}</td></tr>`);
  }
  const corrected = pick(metrics, "minilisysm_memory_ecc_corrected_total");
  const uncorrected = pick(metrics, "minilisysm_memory_ecc_uncorrected_total");
  if (Number.isFinite(corrected) || Number.isFinite(uncorrected)) {
    rows.push(`<tr><td>内存</td><td>EDAC</td><td>ECC corrected ${format(corrected, 0)} / uncorrected ${format(uncorrected, 0)}</td><td class="${uncorrected > 0 ? "bad-text" : "ok-text"}">${uncorrected > 0 ? "异常" : "正常"}</td></tr>`);
  }
  document.getElementById("hardware").innerHTML = rows.length ? rows.join("") : `<tr><td colspan="4">当前环境暂无电池、存储寿命或 EDAC 指标</td></tr>`;
}

function updateLiveState(metrics) {
  const up = pick(metrics, "minilisysm_up");
  const dot = document.getElementById("live-dot");
  const text = document.getElementById("live-text");
  dot.className = `dot ${up === 1 ? "ok" : "bad"}`;
  text.textContent = up === 1 ? "服务运行中" : "服务异常";
}

async function refresh() {
  const updated = document.getElementById("updated");
  try {
    const response = await fetch("/metrics", { cache: "no-store" });
    const text = await response.text();
    const metrics = parseMetrics(text);
    updateLiveState(metrics);
    renderCards(metrics);
    renderQueues(metrics);
    renderCollectors(metrics);
    renderHardware(metrics);
    document.getElementById("raw").textContent = text.slice(0, 12000);
    updated.textContent = `刷新于 ${new Date().toLocaleTimeString()}`;
  } catch (err) {
    document.getElementById("live-dot").className = "dot bad";
    document.getElementById("live-text").textContent = "刷新失败";
    updated.textContent = `刷新失败：${err}`;
  }
}

refresh();
setInterval(refresh, 2000);
</script>
</body>
</html>
)HTML";
}

} // namespace

MetricsServer::MetricsServer(const MonitorConfig& config, RenderCallback render)
    : config_(config), render_(std::move(render)) {}

MetricsServer::~MetricsServer() {
    stop();
}

bool MetricsServer::start() {
    if (!config_.metrics_enable) {
        spdlog::info("metrics server disabled");
        return true;
    }
#if defined(__linux__)
    for (const std::string& client : config_.metrics_allowed_clients) {
        in_addr address{};
        if (::inet_pton(AF_INET, client.c_str(), &address) != 1) {
            spdlog::error("metrics server allowed client is invalid: client={}", client);
            return false;
        }
    }
#endif
    if (!open_listener()) {
        spdlog::error("metrics server failed to open listener: host={} port={}", config_.metrics_bind_host,
                      config_.metrics_port);
        return false;
    }
    running_.store(true);
    worker_ = std::thread(&MetricsServer::run, this);
    spdlog::info("metrics server started: metrics=http://{}:{}/metrics status=http://{}:{}/status",
                 config_.metrics_bind_host, config_.metrics_port, config_.metrics_bind_host, config_.metrics_port);
    return true;
}

void MetricsServer::stop() {
    const bool was_running = running_.exchange(false);
    close_listener();
    if (worker_.joinable()) {
        worker_.join();
    }
    if (was_running) {
        spdlog::info("metrics server stopped");
    }
}

void MetricsServer::run() {
#if defined(__linux__)
    while (running_.load()) {
        pollfd pfd{};
        pfd.fd = listen_fd_;
        pfd.events = POLLIN;
        const int rc = ::poll(&pfd, 1, 200);
        if (rc <= 0 || (pfd.revents & POLLIN) == 0) {
            continue;
        }
        sockaddr_in client_addr{};
        socklen_t client_len = sizeof(client_addr);
        const int client_fd = ::accept(listen_fd_, reinterpret_cast<sockaddr*>(&client_addr), &client_len);
        if (client_fd < 0) {
            spdlog::debug("metrics server accept failed");
            continue;
        }
        char client_address[INET_ADDRSTRLEN]{};
        if (::inet_ntop(AF_INET, &client_addr.sin_addr, client_address, sizeof(client_address)) == nullptr) {
            spdlog::debug("metrics server failed to format client address");
            ::close(client_fd);
            continue;
        }
        handle_client(client_fd, client_address);
    }
#else
    while (running_.load()) {
        std::this_thread::sleep_for(std::chrono::milliseconds(200));
    }
#endif
}

bool MetricsServer::open_listener() {
#if defined(__linux__)
    listen_fd_ = ::socket(AF_INET, SOCK_STREAM | SOCK_CLOEXEC, 0);
    if (listen_fd_ < 0) {
        spdlog::error("metrics server socket creation failed");
        return false;
    }
    int reuse = 1;
    ::setsockopt(listen_fd_, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(config_.metrics_port);
    if (config_.metrics_bind_host.empty() || config_.metrics_bind_host == "0.0.0.0") {
        addr.sin_addr.s_addr = htonl(INADDR_ANY);
    } else if (::inet_pton(AF_INET, config_.metrics_bind_host.c_str(), &addr.sin_addr) != 1) {
        spdlog::error("metrics server bind host is invalid: host={}", config_.metrics_bind_host);
        close_listener();
        return false;
    }
    if (::bind(listen_fd_, reinterpret_cast<const sockaddr*>(&addr), sizeof(addr)) != 0) {
        spdlog::error("metrics server bind failed: host={} port={}", config_.metrics_bind_host, config_.metrics_port);
        close_listener();
        return false;
    }
    if (::listen(listen_fd_, 16) != 0) {
        spdlog::error("metrics server listen failed: host={} port={}", config_.metrics_bind_host, config_.metrics_port);
        close_listener();
        return false;
    }
    return true;
#else
    return false;
#endif
}

void MetricsServer::close_listener() {
#if defined(__linux__)
    if (listen_fd_ >= 0) {
        ::shutdown(listen_fd_, SHUT_RDWR);
        ::close(listen_fd_);
        listen_fd_ = -1;
    }
#endif
}

bool MetricsServer::is_client_allowed(const std::string& client_address) const {
    if (config_.metrics_allowed_clients.empty()) {
        return true;
    }
#if defined(__linux__)
    for (const std::string& client : config_.metrics_allowed_clients) {
        in_addr address{};
        char normalized[INET_ADDRSTRLEN]{};
        if (::inet_pton(AF_INET, client.c_str(), &address) == 1 &&
            ::inet_ntop(AF_INET, &address, normalized, sizeof(normalized)) != nullptr && client_address == normalized) {
            return true;
        }
    }
#else
    (void)client_address;
#endif
    return false;
}

void MetricsServer::handle_client(int client_fd, const std::string& client_address) {
#if defined(__linux__)
    const auto send_response = [client_fd](const std::string& response) {
        const char* data = response.data();
        size_t remaining = response.size();
        while (remaining > 0) {
            const ssize_t sent = ::send(client_fd, data, remaining, MSG_NOSIGNAL);
            if (sent <= 0) {
                break;
            }
            data += sent;
            remaining -= static_cast<size_t>(sent);
        }
    };
    if (!is_client_allowed(client_address)) {
        send_response(http_response(403, "Forbidden", "text/plain; charset=utf-8", "forbidden\n"));
        ::close(client_fd);
        return;
    }
    timeval timeout{};
    timeout.tv_sec = 1;
    (void)::setsockopt(client_fd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
    (void)::setsockopt(client_fd, SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof(timeout));
    char buffer[1024]{};
    const ssize_t n = ::recv(client_fd, buffer, sizeof(buffer) - 1, 0);
    std::string response;
    if (n > 0) {
        response = response_for_request(std::string(buffer, static_cast<size_t>(n)));
    } else {
        response = http_response(400, "Bad Request", "text/plain; charset=utf-8", "bad request\n");
    }
    send_response(response);
    ::close(client_fd);
#else
    (void)client_fd;
    (void)client_address;
#endif
}

std::string MetricsServer::response_for_request(const std::string& request) const {
    if (request.rfind("GET / ", 0) == 0 || request.rfind("GET /status ", 0) == 0 ||
        request.rfind("GET /status?", 0) == 0) {
        return http_response(200, "OK", "text/html; charset=utf-8", status_page_html());
    }
    if (request.rfind("GET /metrics ", 0) == 0 || request.rfind("GET /metrics?", 0) == 0) {
        return http_response(200, "OK", "text/plain; version=0.0.4; charset=utf-8", render_());
    }
    if (request.rfind("GET /healthz ", 0) == 0) {
        return http_response(200, "OK", "text/plain; charset=utf-8", "ok\n");
    }
    return http_response(404, "Not Found", "text/plain; charset=utf-8", "not found\n");
}

} // namespace lisysm
