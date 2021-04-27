/* SPDX-License-Identifier: LGPL-2.1+ */

#include <sys/types.h>

#include "alloc-util.h"
#include "device-nodes.h"
#include "string-util.h"
#include "util.h"

/* helpers for test_encode_devnode_name */
static char *do_encode_string(const char *in) {
        size_t out_len = strlen(in) * 4 + 1;
        char *out = malloc(out_len);

        assert_se(out);
        assert_se(encode_devnode_name(in, out, out_len) >= 0);
        puts(out);

        return out;
}

static bool expect_encoded_as(const char *in, const char *expected) {
        _cleanup_free_ char *encoded = do_encode_string(in);
        return streq(encoded, expected);
}

static void test_encode_devnode_name(void) {
        assert_se(expect_encoded_as("systemd sucks", "systemd\\x20sucks"));
        assert_se(expect_encoded_as("pinkiepie", "pinkiepie"));
        assert_se(expect_encoded_as("valíd\\ųtf8", "valíd\\x5cųtf8"));
        assert_se(expect_encoded_as("s/ash/ng", "s\\x2fash\\x2fng"));
        assert_se(expect_encoded_as("/", "\\x2f"));
        assert_se(expect_encoded_as("!", "\\x21"));
}

int main(int argc, char *argv[]) {
        test_encode_devnode_name();

        return 0;
}
