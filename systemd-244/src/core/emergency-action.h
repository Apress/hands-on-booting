/* SPDX-License-Identifier: LGPL-2.1+ */
#pragma once

typedef enum EmergencyAction {
        EMERGENCY_ACTION_NONE,
        EMERGENCY_ACTION_REBOOT,
        EMERGENCY_ACTION_REBOOT_FORCE,
        EMERGENCY_ACTION_REBOOT_IMMEDIATE,
        EMERGENCY_ACTION_POWEROFF,
        EMERGENCY_ACTION_POWEROFF_FORCE,
        EMERGENCY_ACTION_POWEROFF_IMMEDIATE,
        EMERGENCY_ACTION_EXIT,
        _EMERGENCY_ACTION_FIRST_USER_ACTION = EMERGENCY_ACTION_EXIT,
        EMERGENCY_ACTION_EXIT_FORCE,
        _EMERGENCY_ACTION_MAX,
        _EMERGENCY_ACTION_INVALID = -1
} EmergencyAction;

typedef enum EmergencyActionFlags {
        EMERGENCY_ACTION_IS_WATCHDOG = 1 << 0,
        EMERGENCY_ACTION_WARN        = 1 << 1,
} EmergencyActionFlags;

#include "macro.h"
#include "manager.h"

void emergency_action(Manager *m,
                      EmergencyAction action, EmergencyActionFlags options,
                      const char *reboot_arg, int exit_status, const char *reason);

const char* emergency_action_to_string(EmergencyAction i) _const_;
EmergencyAction emergency_action_from_string(const char *s) _pure_;

int parse_emergency_action(const char *value, bool system, EmergencyAction *ret);
