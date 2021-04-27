/* SPDX-License-Identifier: LGPL-2.1+ */

#include <errno.h>
#include <stddef.h>
#include <stdio.h>
#include <syslog.h>

#include "alloc-util.h"
#include "cpu-set-util.h"
#include "dirent-util.h"
#include "errno-util.h"
#include "extract-word.h"
#include "fd-util.h"
#include "log.h"
#include "macro.h"
#include "memory-util.h"
#include "missing_syscall.h"
#include "parse-util.h"
#include "stat-util.h"
#include "string-util.h"
#include "string-table.h"
#include "strv.h"
#include "util.h"

char* cpu_set_to_string(const CPUSet *a) {
        _cleanup_free_ char *str = NULL;
        size_t allocated = 0, len = 0;
        int i, r;

        for (i = 0; (size_t) i < a->allocated * 8; i++) {
                if (!CPU_ISSET_S(i, a->allocated, a->set))
                        continue;

                if (!GREEDY_REALLOC(str, allocated, len + 1 + DECIMAL_STR_MAX(int)))
                        return NULL;

                r = sprintf(str + len, len > 0 ? " %d" : "%d", i);
                assert_se(r > 0);
                len += r;
        }

        return TAKE_PTR(str) ?: strdup("");
}

char *cpu_set_to_range_string(const CPUSet *set) {
        unsigned range_start = 0, range_end;
        _cleanup_free_ char *str = NULL;
        size_t allocated = 0, len = 0;
        bool in_range = false;
        int r;

        for (unsigned i = 0; i < set->allocated * 8; i++)
                if (CPU_ISSET_S(i, set->allocated, set->set)) {
                        if (in_range)
                                range_end++;
                        else {
                                range_start = range_end = i;
                                in_range = true;
                        }
                } else if (in_range) {
                        in_range = false;

                        if (!GREEDY_REALLOC(str, allocated, len + 2 + 2 * DECIMAL_STR_MAX(unsigned)))
                                return NULL;

                        if (range_end > range_start)
                                r = sprintf(str + len, len > 0 ? " %d-%d" : "%d-%d", range_start, range_end);
                        else
                                r = sprintf(str + len, len > 0 ? " %d" : "%d", range_start);
                        assert_se(r > 0);
                        len += r;
                }

        if (in_range) {
                if (!GREEDY_REALLOC(str, allocated, len + 2 + 2 * DECIMAL_STR_MAX(int)))
                        return NULL;

                if (range_end > range_start)
                        r = sprintf(str + len, len > 0 ? " %d-%d" : "%d-%d", range_start, range_end);
                else
                        r = sprintf(str + len, len > 0 ? " %d" : "%d", range_start);
                assert_se(r > 0);
        }

        return TAKE_PTR(str) ?: strdup("");
}

int cpu_set_realloc(CPUSet *cpu_set, unsigned ncpus) {
        size_t need;

        assert(cpu_set);

        need = CPU_ALLOC_SIZE(ncpus);
        if (need > cpu_set->allocated) {
                cpu_set_t *t;

                t = realloc(cpu_set->set, need);
                if (!t)
                        return -ENOMEM;

                memzero((uint8_t*) t + cpu_set->allocated, need - cpu_set->allocated);

                cpu_set->set = t;
                cpu_set->allocated = need;
        }

        return 0;
}

static int cpu_set_add(CPUSet *cpu_set, unsigned cpu) {
        int r;

        if (cpu >= 8192)
                /* As of kernel 5.1, CONFIG_NR_CPUS can be set to 8192 on PowerPC */
                return -ERANGE;

        r = cpu_set_realloc(cpu_set, cpu + 1);
        if (r < 0)
                return r;

        CPU_SET_S(cpu, cpu_set->allocated, cpu_set->set);
        return 0;
}

int cpu_set_add_all(CPUSet *a, const CPUSet *b) {
        int r;

        /* Do this backwards, so if we fail, we fail before changing anything. */
        for (unsigned cpu_p1 = b->allocated * 8; cpu_p1 > 0; cpu_p1--)
                if (CPU_ISSET_S(cpu_p1 - 1, b->allocated, b->set)) {
                        r = cpu_set_add(a, cpu_p1 - 1);
                        if (r < 0)
                                return r;
                }

        return 0;
}

int parse_cpu_set_full(
                const char *rvalue,
                CPUSet *cpu_set,
                bool warn,
                const char *unit,
                const char *filename,
                unsigned line,
                const char *lvalue) {

        _cleanup_(cpu_set_reset) CPUSet c = {};
        const char *p = rvalue;

        assert(p);

        for (;;) {
                _cleanup_free_ char *word = NULL;
                unsigned cpu_lower, cpu_upper;
                int r;

                r = extract_first_word(&p, &word, WHITESPACE ",", EXTRACT_UNQUOTE);
                if (r == -ENOMEM)
                        return warn ? log_oom() : -ENOMEM;
                if (r < 0)
                        return warn ? log_syntax(unit, LOG_ERR, filename, line, r, "Invalid value for %s: %s", lvalue, rvalue) : r;
                if (r == 0)
                        break;

                r = parse_range(word, &cpu_lower, &cpu_upper);
                if (r < 0)
                        return warn ? log_syntax(unit, LOG_ERR, filename, line, r, "Failed to parse CPU affinity '%s'", word) : r;

                if (cpu_lower > cpu_upper) {
                        if (warn)
                                log_syntax(unit, LOG_WARNING, filename, line, 0, "Range '%s' is invalid, %u > %u, ignoring.",
                                           word, cpu_lower, cpu_upper);

                        /* Make sure something is allocated, to distinguish this from the empty case */
                        r = cpu_set_realloc(&c, 1);
                        if (r < 0)
                                return r;
                }

                for (unsigned cpu_p1 = MIN(cpu_upper, UINT_MAX-1) + 1; cpu_p1 > cpu_lower; cpu_p1--) {
                        r = cpu_set_add(&c, cpu_p1 - 1);
                        if (r < 0)
                                return warn ? log_syntax(unit, LOG_ERR, filename, line, r,
                                                         "Cannot add CPU %u to set: %m", cpu_p1 - 1) : r;
                }
        }

        /* On success, transfer ownership to the output variable */
        *cpu_set = c;
        c = (CPUSet) {};

        return 0;
}

int parse_cpu_set_extend(
                const char *rvalue,
                CPUSet *old,
                bool warn,
                const char *unit,
                const char *filename,
                unsigned line,
                const char *lvalue) {

        _cleanup_(cpu_set_reset) CPUSet cpuset = {};
        int r;

        r = parse_cpu_set_full(rvalue, &cpuset, true, unit, filename, line, lvalue);
        if (r < 0)
                return r;

        if (!cpuset.set) {
                /* An empty assignment resets the CPU list */
                cpu_set_reset(old);
                return 0;
        }

        if (!old->set) {
                *old = cpuset;
                cpuset = (CPUSet) {};
                return 0;
        }

        return cpu_set_add_all(old, &cpuset);
}

int cpus_in_affinity_mask(void) {
        size_t n = 16;
        int r;

        for (;;) {
                cpu_set_t *c;

                c = CPU_ALLOC(n);
                if (!c)
                        return -ENOMEM;

                if (sched_getaffinity(0, CPU_ALLOC_SIZE(n), c) >= 0) {
                        int k;

                        k = CPU_COUNT_S(CPU_ALLOC_SIZE(n), c);
                        CPU_FREE(c);

                        if (k <= 0)
                                return -EINVAL;

                        return k;
                }

                r = -errno;
                CPU_FREE(c);

                if (r != -EINVAL)
                        return r;
                if (n > SIZE_MAX/2)
                        return -ENOMEM;
                n *= 2;
        }
}

int cpu_set_to_dbus(const CPUSet *set, uint8_t **ret, size_t *allocated) {
        uint8_t *out;

        assert(set);
        assert(ret);

        out = new0(uint8_t, set->allocated);
        if (!out)
                return -ENOMEM;

        for (unsigned cpu = 0; cpu < set->allocated * 8; cpu++)
                if (CPU_ISSET_S(cpu, set->allocated, set->set))
                        out[cpu / 8] |= 1u << (cpu % 8);

        *ret = out;
        *allocated = set->allocated;
        return 0;
}

int cpu_set_from_dbus(const uint8_t *bits, size_t size, CPUSet *set) {
        _cleanup_(cpu_set_reset) CPUSet s = {};
        int r;

        assert(bits);
        assert(set);

        for (unsigned cpu = size * 8; cpu > 0; cpu--)
                if (bits[(cpu - 1) / 8] & (1u << ((cpu - 1) % 8))) {
                        r = cpu_set_add(&s, cpu - 1);
                        if (r < 0)
                                return r;
                }

        *set = s;
        s = (CPUSet) {};
        return 0;
}

bool numa_policy_is_valid(const NUMAPolicy *policy) {
        assert(policy);

        if (!mpol_is_valid(numa_policy_get_type(policy)))
                return false;

        if (!policy->nodes.set &&
            !IN_SET(numa_policy_get_type(policy), MPOL_DEFAULT, MPOL_LOCAL, MPOL_PREFERRED))
                return false;

        if (policy->nodes.set &&
            numa_policy_get_type(policy) == MPOL_PREFERRED &&
            CPU_COUNT_S(policy->nodes.allocated, policy->nodes.set) != 1)
                return false;

        return true;
}

static int numa_policy_to_mempolicy(const NUMAPolicy *policy, unsigned long *ret_maxnode, unsigned long **ret_nodes) {
        unsigned node, bits = 0, ulong_bits;
        _cleanup_free_ unsigned long *out = NULL;

        assert(policy);
        assert(ret_maxnode);
        assert(ret_nodes);

        if (IN_SET(numa_policy_get_type(policy), MPOL_DEFAULT, MPOL_LOCAL) ||
            (numa_policy_get_type(policy) == MPOL_PREFERRED && !policy->nodes.set)) {
                *ret_nodes = NULL;
                *ret_maxnode = 0;
                return 0;
        }

        bits = policy->nodes.allocated * 8;
        ulong_bits = sizeof(unsigned long) * 8;

        out = new0(unsigned long, DIV_ROUND_UP(policy->nodes.allocated, sizeof(unsigned long)));
        if (!out)
                return -ENOMEM;

        /* We don't make any assumptions about internal type libc is using to store NUMA node mask.
           Hence we need to convert the node mask to the representation expected by set_mempolicy() */
        for (node = 0; node < bits; node++)
                if (CPU_ISSET_S(node, policy->nodes.allocated, policy->nodes.set))
                        out[node / ulong_bits] |= 1ul << (node % ulong_bits);

        *ret_nodes = TAKE_PTR(out);
        *ret_maxnode = bits + 1;
        return 0;
}

int apply_numa_policy(const NUMAPolicy *policy) {
        int r;
        _cleanup_free_ unsigned long *nodes = NULL;
        unsigned long maxnode;

        assert(policy);

        if (get_mempolicy(NULL, NULL, 0, 0, 0) < 0 && errno == ENOSYS)
                return -EOPNOTSUPP;

        if (!numa_policy_is_valid(policy))
                return -EINVAL;

        r = numa_policy_to_mempolicy(policy, &maxnode, &nodes);
        if (r < 0)
                return r;

        r = set_mempolicy(numa_policy_get_type(policy), nodes, maxnode);
        if (r < 0)
                return -errno;

        return 0;
}

static const char* const mpol_table[] = {
        [MPOL_DEFAULT]    = "default",
        [MPOL_PREFERRED]  = "preferred",
        [MPOL_BIND]       = "bind",
        [MPOL_INTERLEAVE] = "interleave",
        [MPOL_LOCAL]      = "local",
};

DEFINE_STRING_TABLE_LOOKUP(mpol, int);
