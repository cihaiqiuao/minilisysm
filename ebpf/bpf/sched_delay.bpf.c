// SPDX-License-Identifier: MIT
// Minimal sched_switch tracepoint program for the optional eBPF collector.

#include "vmlinux.h"
#include <bpf/bpf_helpers.h>
#include <bpf/bpf_tracing.h>

char LICENSE[] SEC("license") = "MIT";

#ifndef MINILISYSM_EBPF_RINGBUF_BYTES
#define MINILISYSM_EBPF_RINGBUF_BYTES (1024 * 1024)
#endif

#ifndef MINILISYSM_EBPF_AGGREGATE_MAX_ENTRIES
#define MINILISYSM_EBPF_AGGREGATE_MAX_ENTRIES 8192
#endif

struct sched_delay_event {
    __u32 pid;
    __u32 tid;
    __u64 delta_wait_ns;
    __u64 involuntary_switches;
    __u64 max_wait_ns;
    __u64 avg_wait_ns;
    __u64 aggregate_count;
    __u32 flags;
};

struct sched_delay_config {
    __u64 min_wait_ns;
    __u64 aggregate_window_ns;
    __u32 max_events_per_poll;
    __u32 enable_pid_filter;
    __u32 enable_tid_filter;
    __u32 enable_lifecycle;
    __u32 enable_aggregate;
    __u32 aggregate_max_entries;
};

struct sched_delay_counters {
    __u64 ringbuf_drops;
    __u64 allowlist_exec_seen;
    __u64 allowlist_exit_cleaned;
    __u64 allowlist_stale_hits;
    __u64 aggregate_drops;
};

struct wait_start {
    __u64 start_ns;
    __u32 pid;
};

struct aggregate_key {
    __u32 pid;
    __u32 tid;
};

struct aggregate_value {
    __u64 count;
    __u64 sum_wait_ns;
    __u64 max_wait_ns;
    __u64 last_report_ns;
    __u64 drops;
};

struct {
    __uint(type, BPF_MAP_TYPE_RINGBUF);
    __uint(max_entries, MINILISYSM_EBPF_RINGBUF_BYTES);
} events SEC(".maps");

struct {
    __uint(type, BPF_MAP_TYPE_ARRAY);
    __uint(max_entries, 1);
    __type(key, __u32);
    __type(value, struct sched_delay_config);
} bpf_config SEC(".maps");

struct {
    __uint(type, BPF_MAP_TYPE_ARRAY);
    __uint(max_entries, 1);
    __type(key, __u32);
    __type(value, struct sched_delay_counters);
} counters SEC(".maps");

struct {
    __uint(type, BPF_MAP_TYPE_HASH);
    __uint(max_entries, 8192);
    __type(key, __u32);
    __type(value, __u8);
} pid_allowlist SEC(".maps");

struct {
    __uint(type, BPF_MAP_TYPE_HASH);
    __uint(max_entries, 8192);
    __type(key, __u32);
    __type(value, __u8);
} tid_allowlist SEC(".maps");

struct {
    __uint(type, BPF_MAP_TYPE_HASH);
    __uint(max_entries, 8192);
    __type(key, __u32);
    __type(value, struct wait_start);
} runnable_since SEC(".maps");

struct {
    __uint(type, BPF_MAP_TYPE_HASH);
    __uint(max_entries, MINILISYSM_EBPF_AGGREGATE_MAX_ENTRIES);
    __type(key, struct aggregate_key);
    __type(value, struct aggregate_value);
} aggregates SEC(".maps");

static __always_inline struct sched_delay_counters* lookup_counters(void) {
    const __u32 key = 0;
    return bpf_map_lookup_elem(&counters, &key);
}

static __always_inline void increment_ringbuf_drop(void) {
    struct sched_delay_counters* current = lookup_counters();
    if (!current) {
        return;
    }
    __sync_fetch_and_add(&current->ringbuf_drops, 1);
}

static __always_inline int emit_event(__u32 pid, __u32 tid, __u64 wait_ns, __u64 switches, __u64 max_wait_ns,
                                      __u64 avg_wait_ns, __u64 aggregate_count, __u32 flags) {
    struct sched_delay_event* event = bpf_ringbuf_reserve(&events, sizeof(*event), 0);
    if (!event) {
        increment_ringbuf_drop();
        return 0;
    }
    event->pid = pid;
    event->tid = tid;
    event->delta_wait_ns = wait_ns;
    event->involuntary_switches = switches;
    event->max_wait_ns = max_wait_ns;
    event->avg_wait_ns = avg_wait_ns;
    event->aggregate_count = aggregate_count;
    event->flags = flags;
    bpf_ringbuf_submit(event, 0);
    return 1;
}

static __always_inline int aggregate_or_emit(struct sched_delay_config* active_config, __u32 pid, __u32 tid,
                                             __u64 delta_wait_ns, __u64 now) {
    if (!active_config || !active_config->enable_aggregate) {
        return emit_event(pid, tid, delta_wait_ns, 1, delta_wait_ns, delta_wait_ns, 1, 0);
    }

    struct aggregate_key key = {
        .pid = pid,
        .tid = tid,
    };
    struct aggregate_value* existing = bpf_map_lookup_elem(&aggregates, &key);
    if (!existing) {
        struct aggregate_value initial = {
            .count = 1,
            .sum_wait_ns = delta_wait_ns,
            .max_wait_ns = delta_wait_ns,
            .last_report_ns = now,
            .drops = 0,
        };
        if (bpf_map_update_elem(&aggregates, &key, &initial, BPF_NOEXIST) != 0) {
            struct sched_delay_counters* current = lookup_counters();
            if (current) {
                __sync_fetch_and_add(&current->aggregate_drops, 1);
            }
        }
        return 1;
    }

    existing->count += 1;
    existing->sum_wait_ns += delta_wait_ns;
    if (delta_wait_ns > existing->max_wait_ns) {
        existing->max_wait_ns = delta_wait_ns;
    }
    if (now - existing->last_report_ns < active_config->aggregate_window_ns) {
        return 1;
    }
    const __u64 count = existing->count;
    const __u64 sum_wait_ns = existing->sum_wait_ns;
    const __u64 max_wait_ns = existing->max_wait_ns;
    const __u64 avg_wait_ns = count == 0 ? 0 : sum_wait_ns / count;
    const int emitted = emit_event(pid, tid, sum_wait_ns, count, max_wait_ns, avg_wait_ns, count, 1);
    bpf_map_delete_elem(&aggregates, &key);
    return emitted;
}

SEC("tracepoint/sched/sched_switch")
int on_sched_switch(struct trace_event_raw_sched_switch* ctx) {
    const __u64 now = bpf_ktime_get_ns();
    const __u64 current_pid_tgid = bpf_get_current_pid_tgid();
    const __u32 prev_tid = (__u32)ctx->prev_pid;
    const __u32 next_tid = (__u32)ctx->next_pid;

    if (ctx->prev_state == 0) {
        struct wait_start start = {
            .start_ns = now,
            .pid = (__u32)(current_pid_tgid >> 32),
        };
        bpf_map_update_elem(&runnable_since, &prev_tid, &start, BPF_ANY);
    }

    struct wait_start* since = bpf_map_lookup_elem(&runnable_since, &next_tid);
    if (!since) {
        return 0;
    }
    const __u64 delta_wait_ns = now - since->start_ns;
    const __u32 key = 0;
    struct sched_delay_config* active_config = bpf_map_lookup_elem(&bpf_config, &key);
    if (active_config && active_config->min_wait_ns > 0 && delta_wait_ns < active_config->min_wait_ns) {
        bpf_map_delete_elem(&runnable_since, &next_tid);
        return 0;
    }
    if (active_config && active_config->enable_pid_filter) {
        __u8* allowed_pid = bpf_map_lookup_elem(&pid_allowlist, &since->pid);
        if (!allowed_pid) {
            struct sched_delay_counters* current = lookup_counters();
            if (current) {
                __sync_fetch_and_add(&current->allowlist_stale_hits, 1);
            }
            bpf_map_delete_elem(&runnable_since, &next_tid);
            return 0;
        }
    }
    if (active_config && active_config->enable_tid_filter) {
        __u8* allowed_tid = bpf_map_lookup_elem(&tid_allowlist, &next_tid);
        if (!allowed_tid) {
            struct sched_delay_counters* current = lookup_counters();
            if (current) {
                __sync_fetch_and_add(&current->allowlist_stale_hits, 1);
            }
            bpf_map_delete_elem(&runnable_since, &next_tid);
            return 0;
        }
    }
    aggregate_or_emit(active_config, since->pid, next_tid, delta_wait_ns, now);
    bpf_map_delete_elem(&runnable_since, &next_tid);
    return 0;
}

SEC("tracepoint/sched/sched_process_exec")
int on_sched_process_exec(struct trace_event_raw_sched_process_exec* ctx) {
    const __u32 key = 0;
    struct sched_delay_config* active_config = bpf_map_lookup_elem(&bpf_config, &key);
    if (!active_config || !active_config->enable_lifecycle) {
        return 0;
    }
    struct sched_delay_counters* current = lookup_counters();
    if (current) {
        __sync_fetch_and_add(&current->allowlist_exec_seen, 1);
    }
    (void)ctx;
    return 0;
}

SEC("tracepoint/sched/sched_process_exit")
int on_sched_process_exit(struct trace_event_raw_sched_process_template* ctx) {
    const __u32 key = 0;
    struct sched_delay_config* active_config = bpf_map_lookup_elem(&bpf_config, &key);
    if (!active_config || !active_config->enable_lifecycle) {
        return 0;
    }
    const __u64 pid_tgid = bpf_get_current_pid_tgid();
    const __u32 pid = (__u32)(pid_tgid >> 32);
    const __u32 tid = (__u32)pid_tgid;
    (void)ctx;
    bpf_map_delete_elem(&tid_allowlist, &tid);
    bpf_map_delete_elem(&pid_allowlist, &pid);
    bpf_map_delete_elem(&runnable_since, &tid);
    struct sched_delay_counters* current = lookup_counters();
    if (current) {
        __sync_fetch_and_add(&current->allowlist_exit_cleaned, 1);
    }
    return 0;
}
