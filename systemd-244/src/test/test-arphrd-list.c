/* SPDX-License-Identifier: LGPL-2.1+ */

#include <linux/if_arp.h>

#include "string-util.h"
#include "tests.h"

#include "arphrd-list.h"

int main(int argc, const char *argv[]) {
        test_setup_logging(LOG_INFO);

        for (int i = 0; i <= ARPHRD_VOID + 1; i++) {
                const char *name;

                name = arphrd_to_name(i);
                if (name) {
                        log_info("%i: %s", i, name);

                        assert_se(arphrd_from_name(name) == i);
                }
        }

        assert_se(arphrd_to_name(ARPHRD_VOID + 1) == NULL);
        assert_se(arphrd_from_name("huddlduddl") == -EINVAL);
        assert_se(arphrd_from_name("") == -EINVAL);

        return 0;
}
