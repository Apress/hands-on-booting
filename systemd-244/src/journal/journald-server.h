/* SPDX-License-Identifier: LGPL-2.1+ */
#pragma once

#include <stdbool.h>
#include <sys/types.h>

#include "sd-event.h"

typedef struct Server Server;

#include "conf-parser.h"
#include "hashmap.h"
#include "journal-file.h"
#include "journald-context.h"
#include "journald-rate-limit.h"
#include "journald-stream.h"
#include "list.h"
#include "prioq.h"
#include "time-util.h"
#include "varlink.h"

typedef enum Storage {
        STORAGE_AUTO,
        STORAGE_VOLATILE,
        STORAGE_PERSISTENT,
        STORAGE_NONE,
        _STORAGE_MAX,
        _STORAGE_INVALID = -1
} Storage;

typedef enum SplitMode {
        SPLIT_UID,
        SPLIT_LOGIN, /* deprecated */
        SPLIT_NONE,
        _SPLIT_MAX,
        _SPLIT_INVALID = -1
} SplitMode;

typedef struct JournalCompressOptions {
        bool enabled;
        uint64_t threshold_bytes;
} JournalCompressOptions;

typedef struct JournalStorageSpace {
        usec_t   timestamp;

        uint64_t available;
        uint64_t limit;

        uint64_t vfs_used; /* space used by journal files */
        uint64_t vfs_available;
} JournalStorageSpace;

typedef struct JournalStorage {
        const char *name;
        char *path;

        JournalMetrics metrics;
        JournalStorageSpace space;
} JournalStorage;

struct Server {
        int syslog_fd;
        int native_fd;
        int stdout_fd;
        int dev_kmsg_fd;
        int audit_fd;
        int hostname_fd;
        int notify_fd;

        sd_event *event;

        sd_event_source *syslog_event_source;
        sd_event_source *native_event_source;
        sd_event_source *stdout_event_source;
        sd_event_source *dev_kmsg_event_source;
        sd_event_source *audit_event_source;
        sd_event_source *sync_event_source;
        sd_event_source *sigusr1_event_source;
        sd_event_source *sigusr2_event_source;
        sd_event_source *sigterm_event_source;
        sd_event_source *sigint_event_source;
        sd_event_source *sigrtmin1_event_source;
        sd_event_source *hostname_event_source;
        sd_event_source *notify_event_source;
        sd_event_source *watchdog_event_source;

        JournalFile *runtime_journal;
        JournalFile *system_journal;
        OrderedHashmap *user_journals;

        uint64_t seqnum;

        char *buffer;
        size_t buffer_size;

        JournalRateLimit *ratelimit;
        usec_t sync_interval_usec;
        usec_t ratelimit_interval;
        unsigned ratelimit_burst;

        JournalStorage runtime_storage;
        JournalStorage system_storage;

        JournalCompressOptions compress;
        bool seal;
        bool read_kmsg;

        bool forward_to_kmsg;
        bool forward_to_syslog;
        bool forward_to_console;
        bool forward_to_wall;

        unsigned n_forward_syslog_missed;
        usec_t last_warn_forward_syslog_missed;

        usec_t max_retention_usec;
        usec_t max_file_usec;
        usec_t oldest_file_usec;

        LIST_HEAD(StdoutStream, stdout_streams);
        LIST_HEAD(StdoutStream, stdout_streams_notify_queue);
        unsigned n_stdout_streams;

        char *tty_path;

        int max_level_store;
        int max_level_syslog;
        int max_level_kmsg;
        int max_level_console;
        int max_level_wall;

        Storage storage;
        SplitMode split_mode;

        MMapCache *mmap;

        Set *deferred_closes;

        uint64_t *kernel_seqnum;
        bool dev_kmsg_readable:1;

        bool send_watchdog:1;
        bool sent_notify_ready:1;
        bool sync_scheduled:1;

        char machine_id_field[sizeof("_MACHINE_ID=") + 32];
        char boot_id_field[sizeof("_BOOT_ID=") + 32];
        char *hostname_field;

        /* Cached cgroup root, so that we don't have to query that all the time */
        char *cgroup_root;

        usec_t watchdog_usec;

        usec_t last_realtime_clock;

        size_t line_max;

        /* Caching of client metadata */
        Hashmap *client_contexts;
        Prioq *client_contexts_lru;

        usec_t last_cache_pid_flush;

        ClientContext *my_context; /* the context of journald itself */
        ClientContext *pid1_context; /* the context of PID 1 */

        VarlinkServer *varlink_server;
};

#define SERVER_MACHINE_ID(s) ((s)->machine_id_field + STRLEN("_MACHINE_ID="))

/* Extra fields for any log messages */
#define N_IOVEC_META_FIELDS 22

/* Extra fields for log messages that contain OBJECT_PID= (i.e. log about another process) */
#define N_IOVEC_OBJECT_FIELDS 18

/* Maximum number of fields we'll add in for driver (i.e. internal) messages */
#define N_IOVEC_PAYLOAD_FIELDS 16

/* kmsg: Maximum number of extra fields we'll import from the kernel's /dev/kmsg */
#define N_IOVEC_KERNEL_FIELDS 64

/* kmsg: Maximum number of extra fields we'll import from udev's devices */
#define N_IOVEC_UDEV_FIELDS 32

void server_dispatch_message(Server *s, struct iovec *iovec, size_t n, size_t m, ClientContext *c, const struct timeval *tv, int priority, pid_t object_pid);
void server_driver_message(Server *s, pid_t object_pid, const char *message_id, const char *format, ...) _sentinel_ _printf_(4,0);

/* gperf lookup function */
const struct ConfigPerfItem* journald_gperf_lookup(const char *key, GPERF_LEN_TYPE length);

CONFIG_PARSER_PROTOTYPE(config_parse_storage);
CONFIG_PARSER_PROTOTYPE(config_parse_line_max);
CONFIG_PARSER_PROTOTYPE(config_parse_compress);

const char *storage_to_string(Storage s) _const_;
Storage storage_from_string(const char *s) _pure_;

CONFIG_PARSER_PROTOTYPE(config_parse_split_mode);

const char *split_mode_to_string(SplitMode s) _const_;
SplitMode split_mode_from_string(const char *s) _pure_;

int server_init(Server *s);
void server_done(Server *s);
void server_sync(Server *s);
int server_vacuum(Server *s, bool verbose);
void server_rotate(Server *s);
int server_schedule_sync(Server *s, int priority);
int server_flush_to_var(Server *s, bool require_flag_file);
void server_maybe_append_tags(Server *s);
int server_process_datagram(sd_event_source *es, int fd, uint32_t revents, void *userdata);
void server_space_usage_message(Server *s, JournalStorage *storage);
