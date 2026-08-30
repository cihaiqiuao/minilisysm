# Runtime Reliability Hardening

## Scope

- Added `[rule_runtime] cooldown_sec=60` and `state_ttl_sec=3600`.
- The legacy `[memory_rule] cooldown_sec` remains a fallback when the new section is absent.
- `state_ttl_sec=0` is normalized to 3600 seconds.

## Alert Semantics

- Cooldown is scoped by `rule_id + target` and applies to repeated Warning activation after recovery.
- Critical escalation bypasses cooldown.
- Recovery is emitted only after an activation event was actually published.

## Dynamic State

- CPU, I/O, and scheduler rule contexts expire after the configured TTL.
- CPU, I/O, and scheduler collector baselines use the same TTL.
- Scheduler process/thread command caches expire with the scheduler baselines.

## Metrics Access

- `[metrics] allowed_clients` accepts a comma-separated exact IPv4 allowlist.
- Empty allowlist preserves open LAN behavior.
- Invalid allowlist values make MetricsServer startup fail.
- Accepted HTTP clients use fixed one-second receive and send timeouts.
- This is application-layer filtering only; it is not a firewall, VPN, TLS, or public-internet deployment mechanism.

## Verification

- `git diff --check` passed.
- Release build, CTest, and `bash scripts/verify.sh` were not run: Windows has no CMake/compiler in PATH, and the available WSL instance does not mount the repository at `/mnt/e/minilisysm`.
