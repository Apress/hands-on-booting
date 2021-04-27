/* SPDX-License-Identifier: LGPL-2.1+ */

#include <errno.h>
#include <getopt.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#include "sd-daemon.h"

#include "alloc-util.h"
#include "env-util.h"
#include "format-util.h"
#include "log.h"
#include "main-func.h"
#include "parse-util.h"
#include "pretty-print.h"
#include "string-util.h"
#include "strv.h"
#include "terminal-util.h"
#include "user-util.h"
#include "util.h"

static bool arg_ready = false;
static pid_t arg_pid = 0;
static const char *arg_status = NULL;
static bool arg_booted = false;
static uid_t arg_uid = UID_INVALID;
static gid_t arg_gid = GID_INVALID;

static int help(void) {
        _cleanup_free_ char *link = NULL;
        int r;

        r = terminal_urlify_man("systemd-notify", "1", &link);
        if (r < 0)
                return log_oom();

        printf("%s [OPTIONS...] [VARIABLE=VALUE...]\n"
               "\n%sNotify the init system about service status updates.%s\n\n"
               "  -h --help            Show this help\n"
               "     --version         Show package version\n"
               "     --ready           Inform the init system about service start-up completion\n"
               "     --pid[=PID]       Set main PID of daemon\n"
               "     --uid=USER        Set user to send from\n"
               "     --status=TEXT     Set status text\n"
               "     --booted          Check if the system was booted up with systemd\n"
               "\nSee the %s for details.\n"
               , program_invocation_short_name
               , ansi_highlight(), ansi_normal()
               , link
        );

        return 0;
}

static int parse_argv(int argc, char *argv[]) {

        enum {
                ARG_READY = 0x100,
                ARG_VERSION,
                ARG_PID,
                ARG_STATUS,
                ARG_BOOTED,
                ARG_UID,
        };

        static const struct option options[] = {
                { "help",      no_argument,       NULL, 'h'           },
                { "version",   no_argument,       NULL, ARG_VERSION   },
                { "ready",     no_argument,       NULL, ARG_READY     },
                { "pid",       optional_argument, NULL, ARG_PID       },
                { "status",    required_argument, NULL, ARG_STATUS    },
                { "booted",    no_argument,       NULL, ARG_BOOTED    },
                { "uid",       required_argument, NULL, ARG_UID       },
                {}
        };

        int c, r;

        assert(argc >= 0);
        assert(argv);

        while ((c = getopt_long(argc, argv, "h", options, NULL)) >= 0) {

                switch (c) {

                case 'h':
                        return help();

                case ARG_VERSION:
                        return version();

                case ARG_READY:
                        arg_ready = true;
                        break;

                case ARG_PID:

                        if (optarg) {
                                if (parse_pid(optarg, &arg_pid) < 0)
                                        return log_error_errno(SYNTHETIC_ERRNO(EINVAL),
                                                               "Failed to parse PID %s.", optarg);
                        } else
                                arg_pid = getppid();

                        break;

                case ARG_STATUS:
                        arg_status = optarg;
                        break;

                case ARG_BOOTED:
                        arg_booted = true;
                        break;

                case ARG_UID: {
                        const char *u = optarg;

                        r = get_user_creds(&u, &arg_uid, &arg_gid, NULL, NULL, 0);
                        if (r == -ESRCH) /* If the user doesn't exist, then accept it anyway as numeric */
                                r = parse_uid(u, &arg_uid);
                        if (r < 0)
                                return log_error_errno(r, "Can't resolve user %s: %m", optarg);

                        break;
                }

                case '?':
                        return -EINVAL;

                default:
                        assert_not_reached("Unhandled option");
                }
        }

        if (optind >= argc &&
            !arg_ready &&
            !arg_status &&
            !arg_pid &&
            !arg_booted) {
                help();
                return -EINVAL;
        }

        return 1;
}

static int run(int argc, char* argv[]) {
        _cleanup_free_ char *status = NULL, *cpid = NULL, *n = NULL;
        _cleanup_strv_free_ char **final_env = NULL;
        char* our_env[4];
        unsigned i = 0;
        int r;

        log_show_color(true);
        log_parse_environment();
        log_open();

        r = parse_argv(argc, argv);
        if (r <= 0)
                return r;

        if (arg_booted)
                return sd_booted() <= 0;

        if (arg_ready)
                our_env[i++] = (char*) "READY=1";

        if (arg_status) {
                status = strjoin("STATUS=", arg_status);
                if (!status)
                        return log_oom();

                our_env[i++] = status;
        }

        if (arg_pid > 0) {
                if (asprintf(&cpid, "MAINPID="PID_FMT, arg_pid) < 0)
                        return log_oom();

                our_env[i++] = cpid;
        }

        our_env[i++] = NULL;

        final_env = strv_env_merge(2, our_env, argv + optind);
        if (!final_env)
                return log_oom();

        if (strv_isempty(final_env))
                return 0;

        n = strv_join(final_env, "\n");
        if (!n)
                return log_oom();

        /* If this is requested change to the requested UID/GID. Note that we only change the real UID here, and leave
           the effective UID in effect (which is 0 for this to work). That's because we want the privileges to fake the
           ucred data, and sd_pid_notify() uses the real UID for filling in ucred. */

        if (arg_gid != GID_INVALID &&
            setregid(arg_gid, (gid_t) -1) < 0)
                return log_error_errno(errno, "Failed to change GID: %m");

        if (arg_uid != UID_INVALID &&
            setreuid(arg_uid, (uid_t) -1) < 0)
                return log_error_errno(errno, "Failed to change UID: %m");

        r = sd_pid_notify(arg_pid ? arg_pid : getppid(), false, n);
        if (r < 0)
                return log_error_errno(r, "Failed to notify init system: %m");
        if (r == 0)
                return log_error_errno(SYNTHETIC_ERRNO(EOPNOTSUPP),
                                       "No status data could be sent: $NOTIFY_SOCKET was not set");
        return 0;
}

DEFINE_MAIN_FUNCTION(run);
