/* SPDX-License-Identifier: LGPL-2.1+ */

#include <errno.h>
#include <limits.h>
#include <signal.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <sys/epoll.h>
#include <sys/ioctl.h>
#include <sys/time.h>
#include <termios.h>
#include <unistd.h>

#include "sd-event.h"

#include "alloc-util.h"
#include "fd-util.h"
#include "log.h"
#include "macro.h"
#include "ptyfwd.h"
#include "terminal-util.h"
#include "time-util.h"

struct PTYForward {
        sd_event *event;

        int master;

        PTYForwardFlags flags;

        sd_event_source *stdin_event_source;
        sd_event_source *stdout_event_source;
        sd_event_source *master_event_source;

        sd_event_source *sigwinch_event_source;

        struct termios saved_stdin_attr;
        struct termios saved_stdout_attr;

        bool saved_stdin:1;
        bool saved_stdout:1;

        bool stdin_readable:1;
        bool stdin_hangup:1;
        bool stdout_writable:1;
        bool stdout_hangup:1;
        bool master_readable:1;
        bool master_writable:1;
        bool master_hangup:1;

        bool read_from_master:1;

        bool done:1;
        bool drain:1;

        bool last_char_set:1;
        char last_char;

        char in_buffer[LINE_MAX], out_buffer[LINE_MAX];
        size_t in_buffer_full, out_buffer_full;

        usec_t escape_timestamp;
        unsigned escape_counter;

        PTYForwardHandler handler;
        void *userdata;
};

#define ESCAPE_USEC (1*USEC_PER_SEC)

static void pty_forward_disconnect(PTYForward *f) {

        if (f) {
                f->stdin_event_source = sd_event_source_unref(f->stdin_event_source);
                f->stdout_event_source = sd_event_source_unref(f->stdout_event_source);

                f->master_event_source = sd_event_source_unref(f->master_event_source);
                f->sigwinch_event_source = sd_event_source_unref(f->sigwinch_event_source);
                f->event = sd_event_unref(f->event);

                if (f->saved_stdout)
                        tcsetattr(STDOUT_FILENO, TCSANOW, &f->saved_stdout_attr);
                if (f->saved_stdin)
                        tcsetattr(STDIN_FILENO, TCSANOW, &f->saved_stdin_attr);

                f->saved_stdout = f->saved_stdin = false;
        }

        /* STDIN/STDOUT should not be nonblocking normally, so let's unconditionally reset it */
        (void) fd_nonblock(STDIN_FILENO, false);
        (void) fd_nonblock(STDOUT_FILENO, false);
}

static int pty_forward_done(PTYForward *f, int rcode) {
        _cleanup_(sd_event_unrefp) sd_event *e = NULL;
        assert(f);

        if (f->done)
                return 0;

        e = sd_event_ref(f->event);

        f->done = true;
        pty_forward_disconnect(f);

        if (f->handler)
                return f->handler(f, rcode, f->userdata);
        else
                return sd_event_exit(e, rcode < 0 ? EXIT_FAILURE : rcode);
}

static bool look_for_escape(PTYForward *f, const char *buffer, size_t n) {
        const char *p;

        assert(f);
        assert(buffer);
        assert(n > 0);

        for (p = buffer; p < buffer + n; p++) {

                /* Check for ^] */
                if (*p == 0x1D) {
                        usec_t nw = now(CLOCK_MONOTONIC);

                        if (f->escape_counter == 0 || nw > f->escape_timestamp + ESCAPE_USEC)  {
                                f->escape_timestamp = nw;
                                f->escape_counter = 1;
                        } else {
                                (f->escape_counter)++;

                                if (f->escape_counter >= 3)
                                        return true;
                        }
                } else {
                        f->escape_timestamp = 0;
                        f->escape_counter = 0;
                }
        }

        return false;
}

static bool ignore_vhangup(PTYForward *f) {
        assert(f);

        if (f->flags & PTY_FORWARD_IGNORE_VHANGUP)
                return true;

        if ((f->flags & PTY_FORWARD_IGNORE_INITIAL_VHANGUP) && !f->read_from_master)
                return true;

        return false;
}

static bool drained(PTYForward *f) {
        int q = 0;

        assert(f);

        if (f->out_buffer_full > 0)
                return false;

        if (f->master_readable)
                return false;

        if (ioctl(f->master, TIOCINQ, &q) < 0)
                log_debug_errno(errno, "TIOCINQ failed on master: %m");
        else if (q > 0)
                return false;

        if (ioctl(f->master, TIOCOUTQ, &q) < 0)
                log_debug_errno(errno, "TIOCOUTQ failed on master: %m");
        else if (q > 0)
                return false;

        return true;
}

static int shovel(PTYForward *f) {
        ssize_t k;

        assert(f);

        while ((f->stdin_readable && f->in_buffer_full <= 0) ||
               (f->master_writable && f->in_buffer_full > 0) ||
               (f->master_readable && f->out_buffer_full <= 0) ||
               (f->stdout_writable && f->out_buffer_full > 0)) {

                if (f->stdin_readable && f->in_buffer_full < LINE_MAX) {

                        k = read(STDIN_FILENO, f->in_buffer + f->in_buffer_full, LINE_MAX - f->in_buffer_full);
                        if (k < 0) {

                                if (errno == EAGAIN)
                                        f->stdin_readable = false;
                                else if (IN_SET(errno, EIO, EPIPE, ECONNRESET)) {
                                        f->stdin_readable = false;
                                        f->stdin_hangup = true;

                                        f->stdin_event_source = sd_event_source_unref(f->stdin_event_source);
                                } else {
                                        log_error_errno(errno, "read(): %m");
                                        return pty_forward_done(f, -errno);
                                }
                        } else if (k == 0) {
                                /* EOF on stdin */
                                f->stdin_readable = false;
                                f->stdin_hangup = true;

                                f->stdin_event_source = sd_event_source_unref(f->stdin_event_source);
                        } else  {
                                /* Check if ^] has been pressed three times within one second. If we get this we quite
                                 * immediately. */
                                if (look_for_escape(f, f->in_buffer + f->in_buffer_full, k))
                                        return pty_forward_done(f, -ECANCELED);

                                f->in_buffer_full += (size_t) k;
                        }
                }

                if (f->master_writable && f->in_buffer_full > 0) {

                        k = write(f->master, f->in_buffer, f->in_buffer_full);
                        if (k < 0) {

                                if (IN_SET(errno, EAGAIN, EIO))
                                        f->master_writable = false;
                                else if (IN_SET(errno, EPIPE, ECONNRESET)) {
                                        f->master_writable = f->master_readable = false;
                                        f->master_hangup = true;

                                        f->master_event_source = sd_event_source_unref(f->master_event_source);
                                } else {
                                        log_error_errno(errno, "write(): %m");
                                        return pty_forward_done(f, -errno);
                                }
                        } else {
                                assert(f->in_buffer_full >= (size_t) k);
                                memmove(f->in_buffer, f->in_buffer + k, f->in_buffer_full - k);
                                f->in_buffer_full -= k;
                        }
                }

                if (f->master_readable && f->out_buffer_full < LINE_MAX) {

                        k = read(f->master, f->out_buffer + f->out_buffer_full, LINE_MAX - f->out_buffer_full);
                        if (k < 0) {

                                /* Note that EIO on the master device
                                 * might be caused by vhangup() or
                                 * temporary closing of everything on
                                 * the other side, we treat it like
                                 * EAGAIN here and try again, unless
                                 * ignore_vhangup is off. */

                                if (errno == EAGAIN || (errno == EIO && ignore_vhangup(f)))
                                        f->master_readable = false;
                                else if (IN_SET(errno, EPIPE, ECONNRESET, EIO)) {
                                        f->master_readable = f->master_writable = false;
                                        f->master_hangup = true;

                                        f->master_event_source = sd_event_source_unref(f->master_event_source);
                                } else {
                                        log_error_errno(errno, "read(): %m");
                                        return pty_forward_done(f, -errno);
                                }
                        }  else {
                                f->read_from_master = true;
                                f->out_buffer_full += (size_t) k;
                        }
                }

                if (f->stdout_writable && f->out_buffer_full > 0) {

                        k = write(STDOUT_FILENO, f->out_buffer, f->out_buffer_full);
                        if (k < 0) {

                                if (errno == EAGAIN)
                                        f->stdout_writable = false;
                                else if (IN_SET(errno, EIO, EPIPE, ECONNRESET)) {
                                        f->stdout_writable = false;
                                        f->stdout_hangup = true;
                                        f->stdout_event_source = sd_event_source_unref(f->stdout_event_source);
                                } else {
                                        log_error_errno(errno, "write(): %m");
                                        return pty_forward_done(f, -errno);
                                }

                        } else {

                                if (k > 0) {
                                        f->last_char = f->out_buffer[k-1];
                                        f->last_char_set = true;
                                }

                                assert(f->out_buffer_full >= (size_t) k);
                                memmove(f->out_buffer, f->out_buffer + k, f->out_buffer_full - k);
                                f->out_buffer_full -= k;
                        }
                }
        }

        if (f->stdin_hangup || f->stdout_hangup || f->master_hangup) {
                /* Exit the loop if any side hung up and if there's
                 * nothing more to write or nothing we could write. */

                if ((f->out_buffer_full <= 0 || f->stdout_hangup) &&
                    (f->in_buffer_full <= 0 || f->master_hangup))
                        return pty_forward_done(f, 0);
        }

        /* If we were asked to drain, and there's nothing more to handle from the master, then call the callback
         * too. */
        if (f->drain && drained(f))
                return pty_forward_done(f, 0);

        return 0;
}

static int on_master_event(sd_event_source *e, int fd, uint32_t revents, void *userdata) {
        PTYForward *f = userdata;

        assert(f);
        assert(e);
        assert(e == f->master_event_source);
        assert(fd >= 0);
        assert(fd == f->master);

        if (revents & (EPOLLIN|EPOLLHUP))
                f->master_readable = true;

        if (revents & (EPOLLOUT|EPOLLHUP))
                f->master_writable = true;

        return shovel(f);
}

static int on_stdin_event(sd_event_source *e, int fd, uint32_t revents, void *userdata) {
        PTYForward *f = userdata;

        assert(f);
        assert(e);
        assert(e == f->stdin_event_source);
        assert(fd >= 0);
        assert(fd == STDIN_FILENO);

        if (revents & (EPOLLIN|EPOLLHUP))
                f->stdin_readable = true;

        return shovel(f);
}

static int on_stdout_event(sd_event_source *e, int fd, uint32_t revents, void *userdata) {
        PTYForward *f = userdata;

        assert(f);
        assert(e);
        assert(e == f->stdout_event_source);
        assert(fd >= 0);
        assert(fd == STDOUT_FILENO);

        if (revents & (EPOLLOUT|EPOLLHUP))
                f->stdout_writable = true;

        return shovel(f);
}

static int on_sigwinch_event(sd_event_source *e, const struct signalfd_siginfo *si, void *userdata) {
        PTYForward *f = userdata;
        struct winsize ws;

        assert(f);
        assert(e);
        assert(e == f->sigwinch_event_source);

        /* The window size changed, let's forward that. */
        if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) >= 0)
                (void) ioctl(f->master, TIOCSWINSZ, &ws);

        return 0;
}

int pty_forward_new(
                sd_event *event,
                int master,
                PTYForwardFlags flags,
                PTYForward **ret) {

        _cleanup_(pty_forward_freep) PTYForward *f = NULL;
        struct winsize ws;
        int r;

        f = new(PTYForward, 1);
        if (!f)
                return -ENOMEM;

        *f = (struct PTYForward) {
                .flags = flags,
                .master = -1,
        };

        if (event)
                f->event = sd_event_ref(event);
        else {
                r = sd_event_default(&f->event);
                if (r < 0)
                        return r;
        }

        if (!(flags & PTY_FORWARD_READ_ONLY)) {
                r = fd_nonblock(STDIN_FILENO, true);
                if (r < 0)
                        return r;

                r = fd_nonblock(STDOUT_FILENO, true);
                if (r < 0)
                        return r;
        }

        r = fd_nonblock(master, true);
        if (r < 0)
                return r;

        f->master = master;

        if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) < 0) {
                /* If we can't get the resolution from the output fd, then use our internal, regular width/height,
                 * i.e. something derived from $COLUMNS and $LINES if set. */

                ws = (struct winsize) {
                        .ws_row = lines(),
                        .ws_col = columns(),
                };
        }

        (void) ioctl(master, TIOCSWINSZ, &ws);

        if (!(flags & PTY_FORWARD_READ_ONLY)) {
                if (tcgetattr(STDIN_FILENO, &f->saved_stdin_attr) >= 0) {
                        struct termios raw_stdin_attr;

                        f->saved_stdin = true;

                        raw_stdin_attr = f->saved_stdin_attr;
                        cfmakeraw(&raw_stdin_attr);
                        raw_stdin_attr.c_oflag = f->saved_stdin_attr.c_oflag;
                        tcsetattr(STDIN_FILENO, TCSANOW, &raw_stdin_attr);
                }

                if (tcgetattr(STDOUT_FILENO, &f->saved_stdout_attr) >= 0) {
                        struct termios raw_stdout_attr;

                        f->saved_stdout = true;

                        raw_stdout_attr = f->saved_stdout_attr;
                        cfmakeraw(&raw_stdout_attr);
                        raw_stdout_attr.c_iflag = f->saved_stdout_attr.c_iflag;
                        raw_stdout_attr.c_lflag = f->saved_stdout_attr.c_lflag;
                        tcsetattr(STDOUT_FILENO, TCSANOW, &raw_stdout_attr);
                }

                r = sd_event_add_io(f->event, &f->stdin_event_source, STDIN_FILENO, EPOLLIN|EPOLLET, on_stdin_event, f);
                if (r < 0 && r != -EPERM)
                        return r;

                if (r >= 0)
                        (void) sd_event_source_set_description(f->stdin_event_source, "ptyfwd-stdin");
        }

        r = sd_event_add_io(f->event, &f->stdout_event_source, STDOUT_FILENO, EPOLLOUT|EPOLLET, on_stdout_event, f);
        if (r == -EPERM)
                /* stdout without epoll support. Likely redirected to regular file. */
                f->stdout_writable = true;
        else if (r < 0)
                return r;
        else
                (void) sd_event_source_set_description(f->stdout_event_source, "ptyfwd-stdout");

        r = sd_event_add_io(f->event, &f->master_event_source, master, EPOLLIN|EPOLLOUT|EPOLLET, on_master_event, f);
        if (r < 0)
                return r;

        (void) sd_event_source_set_description(f->master_event_source, "ptyfwd-master");

        r = sd_event_add_signal(f->event, &f->sigwinch_event_source, SIGWINCH, on_sigwinch_event, f);
        if (r < 0)
                return r;

        (void) sd_event_source_set_description(f->sigwinch_event_source, "ptyfwd-sigwinch");

        *ret = TAKE_PTR(f);

        return 0;
}

PTYForward *pty_forward_free(PTYForward *f) {
        pty_forward_disconnect(f);
        return mfree(f);
}

int pty_forward_get_last_char(PTYForward *f, char *ch) {
        assert(f);
        assert(ch);

        if (!f->last_char_set)
                return -ENXIO;

        *ch = f->last_char;
        return 0;
}

int pty_forward_set_ignore_vhangup(PTYForward *f, bool b) {
        int r;

        assert(f);

        if (!!(f->flags & PTY_FORWARD_IGNORE_VHANGUP) == b)
                return 0;

        SET_FLAG(f->flags, PTY_FORWARD_IGNORE_VHANGUP, b);

        if (!ignore_vhangup(f)) {

                /* We shall now react to vhangup()s? Let's check
                 * immediately if we might be in one */

                f->master_readable = true;
                r = shovel(f);
                if (r < 0)
                        return r;
        }

        return 0;
}

bool pty_forward_get_ignore_vhangup(PTYForward *f) {
        assert(f);

        return !!(f->flags & PTY_FORWARD_IGNORE_VHANGUP);
}

bool pty_forward_is_done(PTYForward *f) {
        assert(f);

        return f->done;
}

void pty_forward_set_handler(PTYForward *f, PTYForwardHandler cb, void *userdata) {
        assert(f);

        f->handler = cb;
        f->userdata = userdata;
}

bool pty_forward_drain(PTYForward *f) {
        assert(f);

        /* Starts draining the forwarder. Specifically:
         *
         * - Returns true if there are no unprocessed bytes from the pty, false otherwise
         *
         * - Makes sure the handler function is called the next time the number of unprocessed bytes hits zero
         */

        f->drain = true;
        return drained(f);
}

int pty_forward_set_priority(PTYForward *f, int64_t priority) {
        int r;
        assert(f);

        r = sd_event_source_set_priority(f->stdin_event_source, priority);
        if (r < 0)
                return r;

        r = sd_event_source_set_priority(f->stdout_event_source, priority);
        if (r < 0)
                return r;

        r = sd_event_source_set_priority(f->master_event_source, priority);
        if (r < 0)
                return r;

        r = sd_event_source_set_priority(f->sigwinch_event_source, priority);
        if (r < 0)
                return r;

        return 0;
}

int pty_forward_set_width_height(PTYForward *f, unsigned width, unsigned height) {
        struct winsize ws;

        assert(f);

        if (width == (unsigned) -1 && height == (unsigned) -1)
                return 0; /* noop */

        if (width != (unsigned) -1 &&
            (width == 0 || width > USHRT_MAX))
                return -ERANGE;

        if (height != (unsigned) -1 &&
            (height == 0 || height > USHRT_MAX))
                return -ERANGE;

        if (width == (unsigned) -1 || height == (unsigned) -1) {
                if (ioctl(f->master, TIOCGWINSZ, &ws) < 0)
                        return -errno;

                if (width != (unsigned) -1)
                        ws.ws_col = width;
                if (height != (unsigned) -1)
                        ws.ws_row = height;
        } else
                ws = (struct winsize) {
                        .ws_row = height,
                        .ws_col = width,
                };

        if (ioctl(f->master, TIOCSWINSZ, &ws) < 0)
                return -errno;

        /* Make sure we ignore SIGWINCH window size events from now on */
        f->sigwinch_event_source = sd_event_source_unref(f->sigwinch_event_source);

        return 0;
}
