/* SPDX-License-Identifier: LGPL-2.1+ */

#include "hexdecoct.h"
#include "random-util.h"
#include "log.h"
#include "tests.h"

static void test_genuine_random_bytes(RandomFlags flags) {
        uint8_t buf[16] = {};
        unsigned i;

        log_info("/* %s */", __func__);

        for (i = 1; i < sizeof buf; i++) {
                assert_se(genuine_random_bytes(buf, i, flags) == 0);
                if (i + 1 < sizeof buf)
                        assert_se(buf[i] == 0);

                hexdump(stdout, buf, i);
        }
}

static void test_pseudo_random_bytes(void) {
        uint8_t buf[16] = {};
        unsigned i;

        log_info("/* %s */", __func__);

        for (i = 1; i < sizeof buf; i++) {
                pseudo_random_bytes(buf, i);
                if (i + 1 < sizeof buf)
                        assert_se(buf[i] == 0);

                hexdump(stdout, buf, i);
        }
}

static void test_rdrand(void) {
        int r, i;

        for (i = 0; i < 10; i++) {
                unsigned long x = 0;

                r = rdrand(&x);
                if (r < 0) {
                        log_error_errno(r, "RDRAND failed: %m");
                        return;
                }

                printf("%lx\n", x);
        }
}

int main(int argc, char **argv) {
        test_setup_logging(LOG_DEBUG);

        test_genuine_random_bytes(RANDOM_EXTEND_WITH_PSEUDO);
        test_genuine_random_bytes(0);
        test_genuine_random_bytes(RANDOM_BLOCK);
        test_genuine_random_bytes(RANDOM_ALLOW_RDRAND);

        test_pseudo_random_bytes();

        test_rdrand();

        return 0;
}
