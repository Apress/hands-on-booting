/* SPDX-License-Identifier: LGPL-2.1+ */

#include <errno.h>
#include <stdio.h>
#include <sys/stat.h>

#include "alloc-util.h"
#include "fileio.h"
#include "log.h"
#include "util.h"

int main(int argc, char *argv[]) {
        struct stat st;
        const char *device;
        _cleanup_free_ char *major_minor = NULL;
        int r;

        if (argc != 2) {
                log_error("This program expects one argument.");
                return EXIT_FAILURE;
        }

        log_setup_service();

        umask(0022);

        /* Refuse to run unless we are in an initrd() */
        if (!in_initrd())
                return EXIT_SUCCESS;

        device = argv[1];

        if (stat(device, &st) < 0) {
                log_error_errno(errno, "Failed to stat '%s': %m", device);
                return EXIT_FAILURE;
        }

        if (!S_ISBLK(st.st_mode)) {
                log_error("Resume device '%s' is not a block device.", device);
                return EXIT_FAILURE;
        }

        if (asprintf(&major_minor, "%d:%d", major(st.st_rdev), minor(st.st_rdev)) < 0) {
                log_oom();
                return EXIT_FAILURE;
        }

        r = write_string_file("/sys/power/resume", major_minor, WRITE_STRING_FILE_DISABLE_BUFFER);
        if (r < 0) {
                log_error_errno(r, "Failed to write '%s' to /sys/power/resume: %m", major_minor);
                return EXIT_FAILURE;
        }

        /*
         * The write above shall not return.
         *
         * However, failed resume is a normal condition (may mean that there is
         * no hibernation image).
         */

        log_info("Could not resume from '%s' (%s).", device, major_minor);
        return EXIT_SUCCESS;
}
