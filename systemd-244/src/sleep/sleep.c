/* SPDX-License-Identifier: LGPL-2.1+ */
/***
  Copyright © 2010-2017 Canonical
  Copyright © 2018 Dell Inc.
***/

#include <errno.h>
#include <fcntl.h>
#include <getopt.h>
#include <linux/fiemap.h>
#include <poll.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/timerfd.h>
#include <unistd.h>

#include "sd-messages.h"

#include "btrfs-util.h"
#include "def.h"
#include "exec-util.h"
#include "fd-util.h"
#include "format-util.h"
#include "fileio.h"
#include "log.h"
#include "main-func.h"
#include "parse-util.h"
#include "pretty-print.h"
#include "sleep-config.h"
#include "stdio-util.h"
#include "string-util.h"
#include "strv.h"
#include "time-util.h"
#include "util.h"

static char* arg_verb = NULL;

STATIC_DESTRUCTOR_REGISTER(arg_verb, freep);

static int write_hibernate_location_info(const HibernateLocation *hibernate_location) {
        char offset_str[DECIMAL_STR_MAX(uint64_t)];
        int r;

        assert(hibernate_location);
        assert(hibernate_location->swap);
        assert(hibernate_location->resume);

        r = write_string_file("/sys/power/resume", hibernate_location->resume, WRITE_STRING_FILE_DISABLE_BUFFER);
        if (r < 0)
                return log_debug_errno(r, "Failed to write partition device to /sys/power/resume for '%s': '%s': %m",
                                       hibernate_location->swap->device, hibernate_location->resume);

        log_debug("Wrote resume= value for %s to /sys/power/resume: %s", hibernate_location->swap->device, hibernate_location->resume);

        /* if it's a swap partition, we're done */
        if (streq(hibernate_location->swap->type, "partition"))
                return r;

        if (!streq(hibernate_location->swap->type, "file"))
                return log_debug_errno(SYNTHETIC_ERRNO(EINVAL),
                                       "Invalid hibernate type: %s", hibernate_location->swap->type);

        /* Only available in 4.17+ */
        if (hibernate_location->resume_offset > 0 && access("/sys/power/resume_offset", W_OK) < 0) {
                if (errno == ENOENT) {
                        log_debug("Kernel too old, can't configure resume_offset for %s, ignoring: %" PRIu64,
                                  hibernate_location->swap->device, hibernate_location->resume_offset);
                        return 0;
                }

                return log_debug_errno(errno, "/sys/power/resume_offset not writeable: %m");
        }

        xsprintf(offset_str, "%" PRIu64, hibernate_location->resume_offset);
        r = write_string_file("/sys/power/resume_offset", offset_str, WRITE_STRING_FILE_DISABLE_BUFFER);
        if (r < 0)
                return log_debug_errno(r, "Failed to write swap file offset to /sys/power/resume_offset for '%s': '%s': %m",
                                       hibernate_location->swap->device, offset_str);

        log_debug("Wrote resume_offset= value for %s to /sys/power/resume_offset: %s", hibernate_location->swap->device, offset_str);

        return 0;
}

static int write_mode(char **modes) {
        int r = 0;
        char **mode;

        STRV_FOREACH(mode, modes) {
                int k;

                k = write_string_file("/sys/power/disk", *mode, WRITE_STRING_FILE_DISABLE_BUFFER);
                if (k >= 0)
                        return 0;

                log_debug_errno(k, "Failed to write '%s' to /sys/power/disk: %m", *mode);
                if (r >= 0)
                        r = k;
        }

        return r;
}

static int write_state(FILE **f, char **states) {
        char **state;
        int r = 0;

        STRV_FOREACH(state, states) {
                int k;

                k = write_string_stream(*f, *state, WRITE_STRING_FILE_DISABLE_BUFFER);
                if (k >= 0)
                        return 0;
                log_debug_errno(k, "Failed to write '%s' to /sys/power/state: %m", *state);
                if (r >= 0)
                        r = k;

                fclose(*f);
                *f = fopen("/sys/power/state", "we");
                if (!*f)
                        return -errno;
        }

        return r;
}

static int execute(char **modes, char **states) {
        char *arguments[] = {
                NULL,
                (char*) "pre",
                arg_verb,
                NULL
        };
        static const char* const dirs[] = {
                SYSTEM_SLEEP_PATH,
                NULL
        };

        _cleanup_fclose_ FILE *f = NULL;
        _cleanup_(hibernate_location_freep) HibernateLocation *hibernate_location = NULL;
        int r;

        /* This file is opened first, so that if we hit an error,
         * we can abort before modifying any state. */
        f = fopen("/sys/power/state", "we");
        if (!f)
                return log_error_errno(errno, "Failed to open /sys/power/state: %m");

        setvbuf(f, NULL, _IONBF, 0);

        /* Configure the hibernation mode */
        if (!strv_isempty(modes)) {
                r = find_hibernate_location(&hibernate_location);
                if (r < 0)
                        return r;
                else if (r == 0) {
                        r = write_hibernate_location_info(hibernate_location);
                        if (r < 0)
                                return log_error_errno(r, "Failed to prepare for hibernation: %m");
                }

                r = write_mode(modes);
                if (r < 0)
                        return log_error_errno(r, "Failed to write mode to /sys/power/disk: %m");;
        }

        (void) execute_directories(dirs, DEFAULT_TIMEOUT_USEC, NULL, NULL, arguments, NULL, EXEC_DIR_PARALLEL | EXEC_DIR_IGNORE_ERRORS);

        log_struct(LOG_INFO,
                   "MESSAGE_ID=" SD_MESSAGE_SLEEP_START_STR,
                   LOG_MESSAGE("Suspending system..."),
                   "SLEEP=%s", arg_verb);

        r = write_state(&f, states);
        if (r < 0)
                log_struct_errno(LOG_ERR, r,
                                 "MESSAGE_ID=" SD_MESSAGE_SLEEP_STOP_STR,
                                 LOG_MESSAGE("Failed to suspend system. System resumed again: %m"),
                                 "SLEEP=%s", arg_verb);
        else
                log_struct(LOG_INFO,
                           "MESSAGE_ID=" SD_MESSAGE_SLEEP_STOP_STR,
                           LOG_MESSAGE("System resumed."),
                           "SLEEP=%s", arg_verb);

        arguments[1] = (char*) "post";
        (void) execute_directories(dirs, DEFAULT_TIMEOUT_USEC, NULL, NULL, arguments, NULL, EXEC_DIR_PARALLEL | EXEC_DIR_IGNORE_ERRORS);

        return r;
}

static int execute_s2h(const SleepConfig *sleep_config) {
        _cleanup_close_ int tfd = -1;
        char buf[FORMAT_TIMESPAN_MAX];
        struct itimerspec ts = {};
        struct pollfd fds;
        int r;

        assert(sleep_config);

        tfd = timerfd_create(CLOCK_BOOTTIME_ALARM, TFD_NONBLOCK|TFD_CLOEXEC);
        if (tfd < 0)
                return log_error_errno(errno, "Error creating timerfd: %m");

        log_debug("Set timerfd wake alarm for %s",
                  format_timespan(buf, sizeof(buf), sleep_config->hibernate_delay_sec, USEC_PER_SEC));

        timespec_store(&ts.it_value, sleep_config->hibernate_delay_sec);

        r = timerfd_settime(tfd, 0, &ts, NULL);
        if (r < 0)
                return log_error_errno(errno, "Error setting hibernate timer: %m");

        r = execute(sleep_config->suspend_modes, sleep_config->suspend_states);
        if (r < 0)
                return r;

        fds = (struct pollfd) {
                .fd = tfd,
                .events = POLLIN,
        };
        r = poll(&fds, 1, 0);
        if (r < 0)
                return log_error_errno(errno, "Error polling timerfd: %m");

        tfd = safe_close(tfd);

        if (!FLAGS_SET(fds.revents, POLLIN)) /* We woke up before the alarm time, we are done. */
                return 0;

        /* If woken up after alarm time, hibernate */
        log_debug("Attempting to hibernate after waking from %s timer",
                  format_timespan(buf, sizeof(buf), sleep_config->hibernate_delay_sec, USEC_PER_SEC));

        r = execute(sleep_config->hibernate_modes, sleep_config->hibernate_states);
        if (r < 0) {
                log_notice("Couldn't hibernate, will try to suspend again.");
                r = execute(sleep_config->suspend_modes, sleep_config->suspend_states);
                if (r < 0) {
                        log_notice("Could neither hibernate nor suspend again, giving up.");
                        return r;
                }
        }

        return 0;
}

static int help(void) {
        _cleanup_free_ char *link = NULL;
        int r;

        r = terminal_urlify_man("systemd-suspend.service", "8", &link);
        if (r < 0)
                return log_oom();

        printf("%s COMMAND\n\n"
               "Suspend the system, hibernate the system, or both.\n\n"
               "  -h --help              Show this help and exit\n"
               "  --version              Print version string and exit\n"
               "\nCommands:\n"
               "  suspend                Suspend the system\n"
               "  hibernate              Hibernate the system\n"
               "  hybrid-sleep           Both hibernate and suspend the system\n"
               "  suspend-then-hibernate Initially suspend and then hibernate\n"
               "                         the system after a fixed period of time\n"
               "\nSee the %s for details.\n"
               , program_invocation_short_name
               , link
        );

        return 0;
}

static int parse_argv(int argc, char *argv[]) {
        enum {
                ARG_VERSION = 0x100,
        };

        static const struct option options[] = {
                { "help",         no_argument,       NULL, 'h'           },
                { "version",      no_argument,       NULL, ARG_VERSION   },
                {}
        };

        int c;

        assert(argc >= 0);
        assert(argv);

        while ((c = getopt_long(argc, argv, "h", options, NULL)) >= 0)
                switch(c) {
                case 'h':
                        return help();

                case ARG_VERSION:
                        return version();

                case '?':
                        return -EINVAL;

                default:
                        assert_not_reached("Unhandled option");
                }

        if (argc - optind != 1)
                return log_error_errno(SYNTHETIC_ERRNO(EINVAL),
                                       "Usage: %s COMMAND",
                                       program_invocation_short_name);

        arg_verb = strdup(argv[optind]);
        if (!arg_verb)
                return log_oom();

        if (!STR_IN_SET(arg_verb, "suspend", "hibernate", "hybrid-sleep", "suspend-then-hibernate"))
                return log_error_errno(SYNTHETIC_ERRNO(EINVAL),
                                       "Unknown command '%s'.", arg_verb);

        return 1 /* work to do */;
}

static int run(int argc, char *argv[]) {
        bool allow;
        char **modes = NULL, **states = NULL;
        _cleanup_(free_sleep_configp) SleepConfig *sleep_config = NULL;
        int r;

        log_setup_service();

        r = parse_argv(argc, argv);
        if (r <= 0)
                return r;

        r = parse_sleep_config(&sleep_config);
        if (r < 0)
                return r;

        r = sleep_settings(arg_verb, sleep_config, &allow, &modes, &states);
        if (r < 0)
                return r;

        if (!allow)
                return log_error_errno(SYNTHETIC_ERRNO(EACCES),
                                       "Sleep mode \"%s\" is disabled by configuration, refusing.",
                                       arg_verb);

        if (streq(arg_verb, "suspend-then-hibernate"))
                return execute_s2h(sleep_config);
        else
                return execute(modes, states);
}

DEFINE_MAIN_FUNCTION(run);
