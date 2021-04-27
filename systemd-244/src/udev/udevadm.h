/* SPDX-License-Identifier: GPL-2.0+ */
#pragma once

#include <stdio.h>

#include "build.h"
#include "macro.h"

int info_main(int argc, char *argv[], void *userdata);
int trigger_main(int argc, char *argv[], void *userdata);
int settle_main(int argc, char *argv[], void *userdata);
int control_main(int argc, char *argv[], void *userdata);
int monitor_main(int argc, char *argv[], void *userdata);
int hwdb_main(int argc, char *argv[], void *userdata);
int test_main(int argc, char *argv[], void *userdata);
int builtin_main(int argc, char *argv[], void *userdata);

static inline int print_version(void) {
        /* Dracut relies on the version being a single integer */
        puts(STRINGIFY(PROJECT_VERSION));
        return 0;
}
