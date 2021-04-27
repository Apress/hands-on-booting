/* SPDX-License-Identifier: LGPL-2.1+ */

#include <netinet/in.h>

#include "macro.h"
#include "ip-protocol-list.h"
#include "stdio-util.h"
#include "string-util.h"

static void test_int(int i) {
        char str[DECIMAL_STR_MAX(int)];

        assert_se(ip_protocol_from_name(ip_protocol_to_name(i)) == i);

        xsprintf(str, "%i", i);
        assert_se(ip_protocol_from_name(ip_protocol_to_name(parse_ip_protocol(str))) == i);
}

static void test_int_fail(int i) {
        char str[DECIMAL_STR_MAX(int)];

        assert_se(!ip_protocol_to_name(i));

        xsprintf(str, "%i", i);
        assert_se(parse_ip_protocol(str) == -EINVAL);
}

static void test_str(const char *s) {
        assert_se(streq(ip_protocol_to_name(ip_protocol_from_name(s)), s));
        assert_se(streq(ip_protocol_to_name(parse_ip_protocol(s)), s));
}

static void test_str_fail(const char *s) {
        assert_se(ip_protocol_from_name(s) == -EINVAL);
        assert_se(parse_ip_protocol(s) == -EINVAL);
}

static void test_parse_ip_protocol(const char *s, int expected) {
        assert_se(parse_ip_protocol(s) == expected);
}

int main(int argc, const char *argv[]) {
        test_int(IPPROTO_TCP);
        test_int(IPPROTO_DCCP);
        test_int_fail(-1);
        test_int_fail(1024 * 1024);

        test_str("sctp");
        test_str("udp");
        test_str_fail("hoge");
        test_str_fail("-1");
        test_str_fail("1000000000");

        test_parse_ip_protocol("sctp", IPPROTO_SCTP);
        test_parse_ip_protocol("ScTp", IPPROTO_SCTP);
        test_parse_ip_protocol("ip", IPPROTO_IP);
        test_parse_ip_protocol("", IPPROTO_IP);
        test_parse_ip_protocol("1", 1);
        test_parse_ip_protocol("0", 0);
        test_parse_ip_protocol("-10", -EINVAL);
        test_parse_ip_protocol("100000000", -EINVAL);

        return 0;
}
