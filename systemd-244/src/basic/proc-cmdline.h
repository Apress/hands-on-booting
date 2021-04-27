/* SPDX-License-Identifier: LGPL-2.1+ */
#pragma once

#include <stdbool.h>

#include "log.h"

typedef enum ProcCmdlineFlags {
        PROC_CMDLINE_STRIP_RD_PREFIX = 1 << 0,
        PROC_CMDLINE_VALUE_OPTIONAL  = 1 << 1,
        PROC_CMDLINE_RD_STRICT       = 1 << 2,
} ProcCmdlineFlags;

typedef int (*proc_cmdline_parse_t)(const char *key, const char *value, void *data);

int proc_cmdline(char **ret);

int proc_cmdline_parse_given(const char *line, proc_cmdline_parse_t parse_item, void *data, ProcCmdlineFlags flags);
int proc_cmdline_parse(const proc_cmdline_parse_t parse, void *userdata, ProcCmdlineFlags flags);

int proc_cmdline_get_key(const char *parameter, ProcCmdlineFlags flags, char **value);
int proc_cmdline_get_bool(const char *key, bool *ret);

int proc_cmdline_get_key_many_internal(ProcCmdlineFlags flags, ...);
#define proc_cmdline_get_key_many(flags, ...) proc_cmdline_get_key_many_internal(flags, __VA_ARGS__, NULL)

char *proc_cmdline_key_startswith(const char *s, const char *prefix);
bool proc_cmdline_key_streq(const char *x, const char *y);

/* A little helper call, to be used in proc_cmdline_parse_t callbacks */
static inline bool proc_cmdline_value_missing(const char *key, const char *value) {
        if (!value) {
                log_warning("Missing argument for %s= kernel command line switch, ignoring.", key);
                return true;
        }

        return false;
}
