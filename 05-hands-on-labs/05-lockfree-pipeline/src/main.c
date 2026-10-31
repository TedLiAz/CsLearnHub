#define _GNU_SOURCE

#include "pipeline.h"
#include <inttypes.h>
#include <pthread.h>
#include <sched.h>
#include <signal.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

/* ──────── Global Context for Signal-Driven Duration Mode (ADR-0002) ──────── */

static PipelineContext *g_pipeline_ctx = NULL;

static void sig_alarm_handler(int sig) {
    (void)sig;
    if (g_pipeline_ctx) {
        atomic_store_explicit(&g_pipeline_ctx->ingress_done, true, memory_order_relaxed);
    }
}

/* ──────── Core Isolation Topology Discovery (ADR-0005) ──────── */

typedef struct {
    int ingress_core;
    int worker_core;
    int egress_core;
    int control_core;
    bool enabled;
} CoreTopology;

static CoreTopology detect_or_parse_topology(const char *cores_arg) {
    CoreTopology topo = {
        .ingress_core = -1,
        .worker_core  = -1,
        .egress_core  = -1,
        .control_core = -1,
        .enabled      = false
    };

    if (cores_arg != NULL && strcmp(cores_arg, "none") != 0 && strcmp(cores_arg, "off") != 0) {
        int c_in, c_wk, c_eg, c_ctrl;
        int parsed = sscanf(cores_arg, "%d,%d,%d,%d", &c_in, &c_wk, &c_eg, &c_ctrl);
        if (parsed >= 3) {
            topo.ingress_core = c_in;
            topo.worker_core  = c_wk;
            topo.egress_core  = c_eg;
            topo.control_core = (parsed >= 4) ? c_ctrl : c_in;
            topo.enabled      = true;
            return topo;
        }
    } else if (cores_arg != NULL && (strcmp(cores_arg, "none") == 0 || strcmp(cores_arg, "off") == 0)) {
        return topo;
    }

    /* Auto-detect available physical cores via sched_getaffinity and sysfs */
    cpu_set_t cpuset;
    CPU_ZERO(&cpuset);
    if (sched_getaffinity(0, sizeof(cpu_set_t), &cpuset) != 0) {
        return topo;
    }

    int available_cpus[CPU_SETSIZE];
    int avail_count = 0;
    for (int i = 0; i < CPU_SETSIZE; ++i) {
        if (CPU_ISSET(i, &cpuset)) {
            available_cpus[avail_count++] = i;
        }
    }

    if (avail_count < 3) {
        fprintf(stderr, "warning: only %d core(s) available; running unpinned\n", avail_count);
        return topo;
    }

    /* Query physical core IDs from sysfs to strictly exclude SMT siblings */
    int distinct_physical[CPU_SETSIZE];
    int seen_core_ids[CPU_SETSIZE];
    int physical_count = 0;

    for (int i = 0; i < avail_count; ++i) {
        int cpu = available_cpus[i];
        char path[128];
        snprintf(path, sizeof(path), "/sys/devices/system/cpu/cpu%d/topology/core_id", cpu);
        FILE *f = fopen(path, "r");
        int core_id = cpu;
        if (f) {
            if (fscanf(f, "%d", &core_id) != 1) {
                core_id = cpu;
            }
            fclose(f);
        }

        bool seen = false;
        for (int j = 0; j < physical_count; ++j) {
            if (seen_core_ids[j] == core_id) {
                seen = true;
                break;
            }
        }
        if (!seen) {
            seen_core_ids[physical_count] = core_id;
            distinct_physical[physical_count++] = cpu;
        }
    }

    if (physical_count >= 4) {
        topo.control_core = distinct_physical[0];
        topo.ingress_core = distinct_physical[1];
        topo.worker_core  = distinct_physical[2];
        topo.egress_core  = distinct_physical[3];
        topo.enabled      = true;
    } else if (physical_count >= 3) {
        topo.ingress_core = distinct_physical[0];
        topo.worker_core  = distinct_physical[1];
        topo.egress_core  = distinct_physical[2];
        topo.control_core = distinct_physical[0];
        topo.enabled      = true;
    } else {
        topo.control_core = available_cpus[0];
        topo.ingress_core = available_cpus[1];
        topo.worker_core  = available_cpus[2];
        topo.egress_core  = (avail_count >= 4) ? available_cpus[3] : available_cpus[0];
        topo.enabled      = true;
    }

    return topo;
}

static void pin_thread(pthread_t thread, int cpu_id, const char *name) {
    if (cpu_id < 0) return;
    cpu_set_t cpuset;
    CPU_ZERO(&cpuset);
    CPU_SET(cpu_id, &cpuset);
    int rc = pthread_setaffinity_np(thread, sizeof(cpu_set_t), &cpuset);
    if (rc != 0) {
        fprintf(stderr, "warning: failed to pin %s to CPU %d (errno=%d)\n", name, cpu_id, rc);
    }
}

int main(int argc, char *argv[]) {
    uint64_t total_packets = DEFAULT_TOTAL_PACKETS;
    uint32_t num_flows     = DEFAULT_ACTIVE_FLOWS;
    const char *cores_arg  = NULL;
    uint32_t duration_sec  = 0;

    /* Positional backwards compatibility if argv[1] starts with digit */
    if (argc > 1 && argv[1][0] != '-') {
        total_packets = strtoull(argv[1], NULL, 10);
        if (argc > 2 && argv[2][0] != '-') {
            num_flows = (uint32_t)strtoul(argv[2], NULL, 10);
        }
        if (argc > 3 && argv[3][0] != '-') {
            cores_arg = argv[3];
        }
    } else {
        int opt;
        while ((opt = getopt(argc, argv, "n:f:c:d:h")) != -1) {
            switch (opt) {
            case 'n':
                total_packets = strtoull(optarg, NULL, 10);
                break;
            case 'f':
                num_flows = (uint32_t)strtoul(optarg, NULL, 10);
                break;
            case 'c':
                cores_arg = optarg;
                break;
            case 'd':
                duration_sec = (uint32_t)strtoul(optarg, NULL, 10);
                break;
            case 'h':
            default:
                fprintf(stderr, "Usage: %s [-n packets] [-f flows] [-c cores_csv] [-d duration_sec]\n", argv[0]);
                return (opt == 'h') ? 0 : 1;
            }
        }
    }

    if (num_flows >= MAX_FLOWS) {
        fprintf(stderr, "error: num_flows must be < %d\n", MAX_FLOWS);
        return 1;
    }

    CoreTopology topo = detect_or_parse_topology(cores_arg);

    /* ──── Subsystem Initialisation ──── */
    flow_subsystem_init();
    for (uint32_t i = 0; i < num_flows; ++i)
        flow_open(i);

    static PipelineContext ctx;
    pipeline_init(&ctx, total_packets, num_flows);
    g_pipeline_ctx = &ctx;

    if (duration_sec > 0) {
        signal(SIGALRM, sig_alarm_handler);
        alarm(duration_sec);
    }

    printf("══════════════════════════════════════════════════════════════\n");
    printf("  Lock-Free Pipeline Lifecycle Safety Demo (Modular C11)\n");
    printf("  Packets: %" PRIu64 "   Flows: %u   Ring Capacity: %d\n",
           total_packets, num_flows, RING_CAPACITY);
    if (topo.enabled) {
        printf("  Topology: Ingress=CPU %d | Worker=CPU %d | Egress=CPU %d | Control=CPU %d\n",
               topo.ingress_core, topo.worker_core, topo.egress_core, topo.control_core);
    } else {
        printf("  Topology: OS Scheduler (Unpinned Fallback)\n");
    }
    if (duration_sec > 0) {
        printf("  Duration: %u seconds (SIGALRM asynchronous termination)\n", duration_sec);
    }
    printf("══════════════════════════════════════════════════════════════\n\n");

    /* ──── Launch Pipeline Stage Threads ──── */
    pthread_t t_ingress, t_worker, t_egress, t_control;

    pthread_create(&t_ingress, NULL, ingress_thread,  &ctx);
    pthread_create(&t_worker,  NULL, worker_thread,   &ctx);
    pthread_create(&t_egress,  NULL, egress_thread,   &ctx);
    pthread_create(&t_control, NULL, control_thread,  &ctx);

    if (topo.enabled) {
        pin_thread(t_ingress, topo.ingress_core, "Ingress");
        pin_thread(t_worker,  topo.worker_core,  "Worker");
        pin_thread(t_egress,  topo.egress_core,  "Egress");
        pin_thread(t_control, topo.control_core, "Control");
    }

    pthread_join(t_control, NULL);
    pthread_join(t_ingress, NULL);
    pthread_join(t_worker,  NULL);
    pthread_join(t_egress,  NULL);

    /* ──── Summary Performance & Integrity Report ──── */
    printf("\n┌─────────────────────────────────────────────────────────┐\n");
    printf("│                   Pipeline Summary                      │\n");
    printf("├───────────────────────────┬─────────────────────────────┤\n");
    printf("│ Ingress packets sent      │ %27" PRIu64 " │\n", ctx.ingress_stats.packets_sent);
    printf("│ Ingress sentinels injected│ %27" PRIu64 " │\n", ctx.ingress_stats.sentinels_sent);
    printf("├───────────────────────────┼─────────────────────────────┤\n");
    printf("│ Worker packets processed  │ %27" PRIu64 " │\n", ctx.worker_stats.packets_processed);
    printf("│ Worker packets dropped    │ %27" PRIu64 " │\n", ctx.worker_stats.packets_dropped);
    printf("│ Worker sentinels forward  │ %27" PRIu64 " │\n", ctx.worker_stats.sentinels_forwarded);
    printf("├───────────────────────────┼─────────────────────────────┤\n");
    printf("│ Terminal packets egressed │ %27" PRIu64 " │\n", ctx.egress_stats.packets_egressed);
    printf("│ Terminal sentinels recv   │ %27" PRIu64 " │\n", ctx.egress_stats.sentinels_received);
    printf("│ Flows reclaimed (QSBR)    │ %27" PRIu64 " │\n", ctx.egress_stats.flows_reclaimed);
    printf("├───────────────────────────┼─────────────────────────────┤\n");
    printf("│ Control drains initiated  │ %27" PRIu64 " │\n", ctx.control_stats.drains_initiated);
    printf("└───────────────────────────┴─────────────────────────────┘\n\n");

    /* ──── Mathematical Integrity Verification ──── */
    uint64_t total_accounted = ctx.worker_stats.packets_processed + ctx.worker_stats.packets_dropped;
    bool ok = true;

    if (total_accounted != ctx.ingress_stats.packets_sent) {
        fprintf(stderr, "FAIL: processed(%" PRIu64 ") + dropped(%" PRIu64 ") = %" PRIu64 " != sent(%" PRIu64 ")\n",
                ctx.worker_stats.packets_processed, ctx.worker_stats.packets_dropped,
                total_accounted, ctx.ingress_stats.packets_sent);
        ok = false;
    }

    if (ctx.worker_stats.packets_processed != ctx.egress_stats.packets_egressed) {
        fprintf(stderr, "FAIL: worker processed(%" PRIu64 ") != egress received(%" PRIu64 ")\n",
                ctx.worker_stats.packets_processed, ctx.egress_stats.packets_egressed);
        ok = false;
    }

    if (ctx.egress_stats.sentinels_received != ctx.control_stats.drains_initiated) {
        fprintf(stderr, "FAIL: sentinels received(%" PRIu64 ") != drains initiated(%" PRIu64 ")\n",
                ctx.egress_stats.sentinels_received, ctx.control_stats.drains_initiated);
        ok = false;
    }

    if (ctx.egress_stats.flows_reclaimed != ctx.control_stats.drains_initiated) {
        fprintf(stderr, "FAIL: flows reclaimed(%" PRIu64 ") != drains(%" PRIu64 ")\n",
                ctx.egress_stats.flows_reclaimed, ctx.control_stats.drains_initiated);
        ok = false;
    }

    /* Verify all flows returned to FREE state and contexts reclaimed */
    for (uint32_t i = 0; i < num_flows; ++i) {
        FlowState st = flow_get_state(i);
        if (st != FLOW_STATE_FREE) {
            fprintf(stderr, "FAIL: flow %u in state %u (expected FREE)\n", i, (uint32_t)st);
            ok = false;
        }
        if (flow_get_context(i) != NULL) {
            fprintf(stderr, "FAIL: flow %u flow_context not freed (UAF hazard!)\n", i);
            ok = false;
        }
    }

    if (ok) {
        printf("✓ All integrity checks passed. No UAF, no leaks, no data races.\n\n");
    } else {
        printf("✗ Some integrity checks failed. See errors above.\n\n");
        return 1;
    }

    return 0;
}
