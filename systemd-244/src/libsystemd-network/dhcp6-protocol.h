/* SPDX-License-Identifier: LGPL-2.1+ */
#pragma once

/***
  Copyright © 2014 Intel Corporation. All rights reserved.
***/

#include <netinet/ip6.h>
#include <netinet/udp.h>

#include "macro.h"
#include "sparse-endian.h"

struct DHCP6Message {
        union {
                struct {
                        uint8_t type;
                        uint8_t _pad[3];
                } _packed_;
                be32_t transaction_id;
        };
        uint8_t options[];
} _packed_;

typedef struct DHCP6Message DHCP6Message;

#define DHCP6_MIN_OPTIONS_SIZE \
        1280 - sizeof(struct ip6_hdr) - sizeof(struct udphdr)

#define IN6ADDR_ALL_DHCP6_RELAY_AGENTS_AND_SERVERS_INIT \
        { { { 0xff, 0x02, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, \
              0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0x00, 0x02 } } }

enum {
        DHCP6_PORT_SERVER                       = 547,
        DHCP6_PORT_CLIENT                       = 546,
};

#define DHCP6_INF_TIMEOUT                       1 * USEC_PER_SEC
#define DHCP6_INF_MAX_RT                        120 * USEC_PER_SEC
#define DHCP6_SOL_MAX_DELAY                     1 * USEC_PER_SEC
#define DHCP6_SOL_TIMEOUT                       1 * USEC_PER_SEC
#define DHCP6_SOL_MAX_RT                        120 * USEC_PER_SEC
#define DHCP6_REQ_TIMEOUT                       1 * USEC_PER_SEC
#define DHCP6_REQ_MAX_RT                        120 * USEC_PER_SEC
#define DHCP6_REQ_MAX_RC                        10
#define DHCP6_REN_TIMEOUT                       10 * USEC_PER_SEC
#define DHCP6_REN_MAX_RT                        600 * USEC_PER_SEC
#define DHCP6_REB_TIMEOUT                       10 * USEC_PER_SEC
#define DHCP6_REB_MAX_RT                        600 * USEC_PER_SEC

enum DHCP6State {
        DHCP6_STATE_STOPPED                     = 0,
        DHCP6_STATE_INFORMATION_REQUEST         = 1,
        DHCP6_STATE_SOLICITATION                = 2,
        DHCP6_STATE_REQUEST                     = 3,
        DHCP6_STATE_BOUND                       = 4,
        DHCP6_STATE_RENEW                       = 5,
        DHCP6_STATE_REBIND                      = 6,
};

enum {
        DHCP6_SOLICIT                           = 1,
        DHCP6_ADVERTISE                         = 2,
        DHCP6_REQUEST                           = 3,
        DHCP6_CONFIRM                           = 4,
        DHCP6_RENEW                             = 5,
        DHCP6_REBIND                            = 6,
        DHCP6_REPLY                             = 7,
        DHCP6_RELEASE                           = 8,
        DHCP6_DECLINE                           = 9,
        DHCP6_RECONFIGURE                       = 10,
        DHCP6_INFORMATION_REQUEST               = 11,
        DHCP6_RELAY_FORW                        = 12,
        DHCP6_RELAY_REPL                        = 13,
        _DHCP6_MESSAGE_MAX                      = 14,
};

enum {
        DHCP6_NTP_SUBOPTION_SRV_ADDR            = 1,
        DHCP6_NTP_SUBOPTION_MC_ADDR             = 2,
        DHCP6_NTP_SUBOPTION_SRV_FQDN            = 3,
};

enum {
        DHCP6_STATUS_SUCCESS                    = 0,
        DHCP6_STATUS_UNSPEC_FAIL                = 1,
        DHCP6_STATUS_NO_ADDRS_AVAIL             = 2,
        DHCP6_STATUS_NO_BINDING                 = 3,
        DHCP6_STATUS_NOT_ON_LINK                = 4,
        DHCP6_STATUS_USE_MULTICAST              = 5,
        _DHCP6_STATUS_MAX                       = 6,
};

enum {
        DHCP6_FQDN_FLAG_S = (1 << 0),
        DHCP6_FQDN_FLAG_O = (1 << 1),
        DHCP6_FQDN_FLAG_N = (1 << 2),
};
