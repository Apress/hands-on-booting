/* SPDX-License-Identifier: LGPL-2.1+ */
#pragma once

#include "sd-bus.h"

typedef struct Link Link;

int wifi_get_info(Link *link);
