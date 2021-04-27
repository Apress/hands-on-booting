/* SPDX-License-Identifier: LGPL-2.1+ */
#pragma once

/***
  Copyright © 2014 Vinay Kulkarni <kulkarniv@vmware.com>
***/

#include "conf-parser.h"

typedef struct Manager Manager;

int manager_parse_config_file(Manager *m);

const struct ConfigPerfItem* networkd_gperf_lookup(const char *key, GPERF_LEN_TYPE length);

CONFIG_PARSER_PROTOTYPE(config_parse_duid_type);
CONFIG_PARSER_PROTOTYPE(config_parse_duid_rawdata);
CONFIG_PARSER_PROTOTYPE(config_parse_ip_service_type);
