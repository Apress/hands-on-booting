/* SPDX-License-Identifier: LGPL-2.1+ */

#include <getopt.h>

#include "main-func.h"
#include "util.h"

static bool arg_verbose = false;

static void help(void) {
        printf("%s\n\n"
               "Report whether we are connected to an external power source.\n\n"
               "  -h --help             Show this help\n"
               "     --version          Show package version\n"
               "  -v --verbose          Show state as text\n"
               , program_invocation_short_name);
}

static int parse_argv(int argc, char *argv[]) {

        enum {
                ARG_VERSION = 0x100,
        };

        static const struct option options[] = {
                { "help",    no_argument, NULL, 'h'         },
                { "version", no_argument, NULL, ARG_VERSION },
                { "verbose", no_argument, NULL, 'v'         },
                {}
        };

        int c;

        assert(argc >= 0);
        assert(argv);

        while ((c = getopt_long(argc, argv, "hv", options, NULL)) >= 0)

                switch (c) {

                case 'h':
                        help();
                        return 0;

                case ARG_VERSION:
                        return version();

                case 'v':
                        arg_verbose = true;
                        break;

                case '?':
                        return -EINVAL;

                default:
                        assert_not_reached("Unhandled option");
                }

        if (optind < argc)
                return log_error_errno(SYNTHETIC_ERRNO(EINVAL),
                                       "%s takes no arguments.",
                                       program_invocation_short_name);

        return 1;
}

static int run(int argc, char *argv[]) {
        int r;

        /* This is mostly intended to be used for scripts which want
         * to detect whether AC power is plugged in or not. */

        log_parse_environment();
        log_open();

        r = parse_argv(argc, argv);
        if (r <= 0)
                return r;

        r = on_ac_power();
        if (r < 0)
                return log_error_errno(r, "Failed to read AC status: %m");

        if (arg_verbose)
                puts(yes_no(r));

        return r == 0;
}

DEFINE_MAIN_FUNCTION_WITH_POSITIVE_FAILURE(run);
