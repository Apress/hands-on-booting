/* SPDX-License-Identifier: LGPL-2.1+ */

#include "log.h"
#include "resolved-dns-packet.h"
#include "tests.h"

static void test_dns_packet_new(void) {
        size_t i;
         _cleanup_(dns_packet_unrefp) DnsPacket *p2 = NULL;

        for (i = 0; i <= DNS_PACKET_SIZE_MAX; i++) {
                _cleanup_(dns_packet_unrefp) DnsPacket *p = NULL;

                assert_se(dns_packet_new(&p, DNS_PROTOCOL_DNS, i, DNS_PACKET_SIZE_MAX) == 0);

                log_debug("dns_packet_new: %zu → %zu", i, p->allocated);
                assert_se(p->allocated >= MIN(DNS_PACKET_SIZE_MAX, i));

                if (i > DNS_PACKET_SIZE_START + 10 && i < DNS_PACKET_SIZE_MAX - 10)
                        i = MIN(i * 2, DNS_PACKET_SIZE_MAX - 10);
        }

        assert_se(dns_packet_new(&p2, DNS_PROTOCOL_DNS, DNS_PACKET_SIZE_MAX + 1, DNS_PACKET_SIZE_MAX) == -EFBIG);
}

int main(int argc, char **argv) {
        test_setup_logging(LOG_DEBUG);

        test_dns_packet_new();

        return 0;
}
