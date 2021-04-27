/* SPDX-License-Identifier: LGPL-2.1+ */
#pragma once

typedef struct Scope Scope;

#include "cgroup.h"
#include "kill.h"
#include "unit.h"

typedef enum ScopeResult {
        SCOPE_SUCCESS,
        SCOPE_FAILURE_RESOURCES,
        SCOPE_FAILURE_TIMEOUT,
        _SCOPE_RESULT_MAX,
        _SCOPE_RESULT_INVALID = -1
} ScopeResult;

struct Scope {
        Unit meta;

        CGroupContext cgroup_context;
        KillContext kill_context;

        ScopeState state, deserialized_state;
        ScopeResult result;

        usec_t runtime_max_usec;
        usec_t timeout_stop_usec;

        char *controller;
        sd_bus_track *controller_track;

        bool was_abandoned;

        sd_event_source *timer_event_source;
};

extern const UnitVTable scope_vtable;

int scope_abandon(Scope *s);

const char* scope_result_to_string(ScopeResult i) _const_;
ScopeResult scope_result_from_string(const char *s) _pure_;

DEFINE_CAST(SCOPE, Scope);
