/* SPDX-License-Identifier: LGPL-2.1+ */

#include <stdio.h>
#include <sys/prctl.h>

#include "alloc-util.h"
#include "cap-list.h"
#include "capability-util.h"
#include "parse-util.h"
#include "string-util.h"
#include "util.h"

/* verify the capability parser */
static void test_cap_list(void) {
        int i;

        assert_se(!capability_to_name(-1));
        assert_se(!capability_to_name(capability_list_length()));

        for (i = 0; i < capability_list_length(); i++) {
                const char *n;

                assert_se(n = capability_to_name(i));
                assert_se(capability_from_name(n) == i);
                printf("%s = %i\n", n, i);
        }

        assert_se(capability_from_name("asdfbsd") == -EINVAL);
        assert_se(capability_from_name("CAP_AUDIT_READ") == CAP_AUDIT_READ);
        assert_se(capability_from_name("cap_audit_read") == CAP_AUDIT_READ);
        assert_se(capability_from_name("cAp_aUdIt_rEAd") == CAP_AUDIT_READ);
        assert_se(capability_from_name("0") == 0);
        assert_se(capability_from_name("15") == 15);
        assert_se(capability_from_name("-1") == -EINVAL);

        for (i = 0; i < capability_list_length(); i++) {
                _cleanup_cap_free_charp_ char *a = NULL;
                const char *b;
                unsigned u;

                assert_se(a = cap_to_name(i));

                /* quit the loop as soon as libcap starts returning
                 * numeric ids, formatted as strings */
                if (safe_atou(a, &u) >= 0)
                        break;

                assert_se(b = capability_to_name(i));

                printf("%s vs. %s\n", a, b);

                assert_se(strcasecmp(a, b) == 0);
        }
}

static void test_capability_set_one(uint64_t c, const char *t) {
        _cleanup_free_ char *t1 = NULL;
        uint64_t c1, c_masked = c & ((UINT64_C(1) << capability_list_length()) - 1);

        assert_se(capability_set_to_string_alloc(c, &t1) == 0);
        assert_se(streq(t1, t));

        assert_se(capability_set_from_string(t1, &c1) == 0);
        assert_se(c1 == c_masked);

        free(t1);
        assert_se(t1 = strjoin("'cap_chown cap_dac_override' \"cap_setgid cap_setuid\"", t,
                               " hogehoge foobar 12345 3.14 -3 ", t));
        assert_se(capability_set_from_string(t1, &c1) == 0);
        assert_se(c1 == c_masked);
}

static void test_capability_set(void) {
        uint64_t c;

        assert_se(capability_set_from_string(NULL, &c) == 0);
        assert_se(c == 0);

        assert_se(capability_set_from_string("", &c) == 0);
        assert_se(c == 0);

        assert_se(capability_set_from_string("0", &c) == 0);
        assert_se(c == UINT64_C(1));

        assert_se(capability_set_from_string("1", &c) == 0);
        assert_se(c == UINT64_C(1) << 1);

        assert_se(capability_set_from_string("0 1 2 3", &c) == 0);
        assert_se(c == (UINT64_C(1) << 4) - 1);

        test_capability_set_one(0, "");
        test_capability_set_one(
                UINT64_C(1) << CAP_DAC_OVERRIDE,
                "cap_dac_override");
        test_capability_set_one(
                UINT64_C(1) << CAP_DAC_OVERRIDE |
                UINT64_C(1) << capability_list_length(),
                "cap_dac_override");
        test_capability_set_one(
                UINT64_C(1) << capability_list_length(), "");
        test_capability_set_one(
                UINT64_C(1) << CAP_CHOWN |
                UINT64_C(1) << CAP_DAC_OVERRIDE |
                UINT64_C(1) << CAP_DAC_READ_SEARCH |
                UINT64_C(1) << CAP_FOWNER |
                UINT64_C(1) << CAP_SETGID |
                UINT64_C(1) << CAP_SETUID |
                UINT64_C(1) << CAP_SYS_PTRACE |
                UINT64_C(1) << CAP_SYS_ADMIN |
                UINT64_C(1) << CAP_AUDIT_CONTROL |
                UINT64_C(1) << CAP_MAC_OVERRIDE |
                UINT64_C(1) << CAP_SYSLOG |
                UINT64_C(1) << (capability_list_length() + 1),
                "cap_chown cap_dac_override cap_dac_read_search cap_fowner "
                "cap_setgid cap_setuid cap_sys_ptrace cap_sys_admin "
                "cap_audit_control cap_mac_override cap_syslog");
}

int main(int argc, char *argv[]) {
        test_cap_list();
        test_capability_set();

        return 0;
}
