# 2026-07-09 浏览器和手机状态页

## 变更

- 在现有 `MetricsServer` 上新增 `/` 和 `/status`，返回轻量 HTML 状态页。
- 状态页每 2 秒从同一服务的 `/metrics` 拉取 Prometheus text，在浏览器端解析并显示 Agent、CPU、内存、队列、事件数和 collector failure 摘要。
- 保留 `/metrics` 和 `/healthz` 现有行为；不改变 metrics 名称、collector 逻辑、事件格式和配置 schema。
- 默认使用 `[metrics] bind_host=0.0.0.0`，让真实 Linux/车端设备在同一可信局域网内可直接访问状态页。

## 访问方式

- 本机访问：`http://127.0.0.1:9108/status`
- 手机或其他调试机访问：确认设备 IP 和防火墙放行端口后访问 `http://<设备IP>:9108/status`。

## 边界

- 当前不是原生手机 App，也不是公网远程管理平台。
- 该 HTTP 服务没有内置认证和 TLS，只适合可信内网、车端调试网络或 VPN 后面访问。

## 验证

- `test_metrics_server` 增加 `/status` 和 `/` 覆盖，确认状态页返回 `200 OK` 且包含页面标题和 `/metrics` 拉取逻辑。
