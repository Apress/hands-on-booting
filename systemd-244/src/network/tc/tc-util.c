/* SPDX-License-Identifier: LGPL-2.1+
 * Copyright © 2019 VMware, Inc. */

#include "alloc-util.h"
#include "fileio.h"
#include "parse-util.h"
#include "tc-util.h"
#include "time-util.h"

static int tc_init(double *ticks_in_usec) {
        uint32_t clock_resolution, ticks_to_usec, usec_to_ticks;
        _cleanup_free_ char *line = NULL;
        double clock_factor;
        int r;

        r = read_one_line_file("/proc/net/psched", &line);
        if (r < 0)
                return r;

        r = sscanf(line, "%08x%08x%08x", &ticks_to_usec, &usec_to_ticks, &clock_resolution);
        if (r < 3)
                return -EIO;

        clock_factor =  (double) clock_resolution / USEC_PER_SEC;
        *ticks_in_usec = (double) ticks_to_usec / usec_to_ticks * clock_factor;

        return 0;
}

int tc_time_to_tick(usec_t t, uint32_t *ret) {
        static double ticks_in_usec = -1;
        usec_t a;
        int r;

        assert(ret);

        if (ticks_in_usec < 0) {
                r = tc_init(&ticks_in_usec);
                if (r < 0)
                        return r;
        }

        a = t * ticks_in_usec;
        if (a > UINT32_MAX)
                return -ERANGE;

        *ret = a;
        return 0;
}

int parse_tc_percent(const char *s, uint32_t *percent)  {
        int r;

        assert(s);
        assert(percent);

        r = parse_permille(s);
        if (r < 0)
                return r;

        *percent = (double) r / 1000 * UINT32_MAX;
        return 0;
}
