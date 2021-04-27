/* SPDX-License-Identifier: LGPL-2.1+ */

#include <fcntl.h>
#include <unistd.h>

#include "sd-journal.h"

#include "alloc-util.h"
#include "chattr-util.h"
#include "journal-file.h"
#include "journal-internal.h"
#include "log.h"
#include "macro.h"
#include "parse-util.h"
#include "rm-rf.h"
#include "tests.h"
#include "util.h"

#define N_ENTRIES 200

static void verify_contents(sd_journal *j, unsigned skip) {
        unsigned i;

        assert_se(j);

        i = 0;
        SD_JOURNAL_FOREACH(j) {
                const void *d;
                char *k, *c;
                size_t l;
                unsigned u = 0;

                assert_se(sd_journal_get_cursor(j, &k) >= 0);
                printf("cursor: %s\n", k);
                free(k);

                assert_se(sd_journal_get_data(j, "MAGIC", &d, &l) >= 0);
                printf("\t%.*s\n", (int) l, (const char*) d);

                assert_se(sd_journal_get_data(j, "NUMBER", &d, &l) >= 0);
                assert_se(k = strndup(d, l));
                printf("\t%s\n", k);

                if (skip > 0) {
                        assert_se(safe_atou(k + 7, &u) >= 0);
                        assert_se(i == u);
                        i += skip;
                }

                free(k);

                assert_se(sd_journal_get_cursor(j, &c) >= 0);
                assert_se(sd_journal_test_cursor(j, c) > 0);
                free(c);
        }

        if (skip > 0)
                assert_se(i == N_ENTRIES);
}

int main(int argc, char *argv[]) {
        JournalFile *one, *two, *three;
        char t[] = "/var/tmp/journal-stream-XXXXXX";
        unsigned i;
        _cleanup_(sd_journal_closep) sd_journal *j = NULL;
        char *z;
        const void *data;
        size_t l;
        dual_timestamp previous_ts = DUAL_TIMESTAMP_NULL;

        /* journal_file_open requires a valid machine id */
        if (access("/etc/machine-id", F_OK) != 0)
                return log_tests_skipped("/etc/machine-id not found");

        test_setup_logging(LOG_DEBUG);

        assert_se(mkdtemp(t));
        assert_se(chdir(t) >= 0);
        (void) chattr_path(t, FS_NOCOW_FL, FS_NOCOW_FL, NULL);

        assert_se(journal_file_open(-1, "one.journal", O_RDWR|O_CREAT, 0666, true, (uint64_t) -1, false, NULL, NULL, NULL, NULL, &one) == 0);
        assert_se(journal_file_open(-1, "two.journal", O_RDWR|O_CREAT, 0666, true, (uint64_t) -1, false, NULL, NULL, NULL, NULL, &two) == 0);
        assert_se(journal_file_open(-1, "three.journal", O_RDWR|O_CREAT, 0666, true, (uint64_t) -1, false, NULL, NULL, NULL, NULL, &three) == 0);

        for (i = 0; i < N_ENTRIES; i++) {
                char *p, *q;
                dual_timestamp ts;
                struct iovec iovec[2];

                dual_timestamp_get(&ts);

                if (ts.monotonic <= previous_ts.monotonic)
                        ts.monotonic = previous_ts.monotonic + 1;

                if (ts.realtime <= previous_ts.realtime)
                        ts.realtime = previous_ts.realtime + 1;

                previous_ts = ts;

                assert_se(asprintf(&p, "NUMBER=%u", i) >= 0);
                iovec[0].iov_base = p;
                iovec[0].iov_len = strlen(p);

                assert_se(asprintf(&q, "MAGIC=%s", i % 5 == 0 ? "quux" : "waldo") >= 0);

                iovec[1].iov_base = q;
                iovec[1].iov_len = strlen(q);

                if (i % 10 == 0)
                        assert_se(journal_file_append_entry(three, &ts, NULL, iovec, 2, NULL, NULL, NULL) == 0);
                else {
                        if (i % 3 == 0)
                                assert_se(journal_file_append_entry(two, &ts, NULL, iovec, 2, NULL, NULL, NULL) == 0);

                        assert_se(journal_file_append_entry(one, &ts, NULL, iovec, 2, NULL, NULL, NULL) == 0);
                }

                free(p);
                free(q);
        }

        (void) journal_file_close(one);
        (void) journal_file_close(two);
        (void) journal_file_close(three);

        assert_se(sd_journal_open_directory(&j, t, 0) >= 0);

        assert_se(sd_journal_add_match(j, "MAGIC=quux", 0) >= 0);
        SD_JOURNAL_FOREACH_BACKWARDS(j) {
                _cleanup_free_ char *c;

                assert_se(sd_journal_get_data(j, "NUMBER", &data, &l) >= 0);
                printf("\t%.*s\n", (int) l, (const char*) data);

                assert_se(sd_journal_get_cursor(j, &c) >= 0);
                assert_se(sd_journal_test_cursor(j, c) > 0);
        }

        SD_JOURNAL_FOREACH(j) {
                _cleanup_free_ char *c;

                assert_se(sd_journal_get_data(j, "NUMBER", &data, &l) >= 0);
                printf("\t%.*s\n", (int) l, (const char*) data);

                assert_se(sd_journal_get_cursor(j, &c) >= 0);
                assert_se(sd_journal_test_cursor(j, c) > 0);
        }

        sd_journal_flush_matches(j);

        verify_contents(j, 1);

        printf("NEXT TEST\n");
        assert_se(sd_journal_add_match(j, "MAGIC=quux", 0) >= 0);

        assert_se(z = journal_make_match_string(j));
        printf("resulting match expression is: %s\n", z);
        free(z);

        verify_contents(j, 5);

        printf("NEXT TEST\n");
        sd_journal_flush_matches(j);
        assert_se(sd_journal_add_match(j, "MAGIC=waldo", 0) >= 0);
        assert_se(sd_journal_add_match(j, "NUMBER=10", 0) >= 0);
        assert_se(sd_journal_add_match(j, "NUMBER=11", 0) >= 0);
        assert_se(sd_journal_add_match(j, "NUMBER=12", 0) >= 0);

        assert_se(z = journal_make_match_string(j));
        printf("resulting match expression is: %s\n", z);
        free(z);

        verify_contents(j, 0);

        assert_se(sd_journal_query_unique(j, "NUMBER") >= 0);
        SD_JOURNAL_FOREACH_UNIQUE(j, data, l)
                printf("%.*s\n", (int) l, (const char*) data);

        assert_se(rm_rf(t, REMOVE_ROOT|REMOVE_PHYSICAL) >= 0);

        return 0;
}
