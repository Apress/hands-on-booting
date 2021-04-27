/* SPDX-License-Identifier: LGPL-2.1+ */

#include "bus-util.h"
#include "dbus-cgroup.h"
#include "dbus-execute.h"
#include "dbus-kill.h"
#include "dbus-mount.h"
#include "dbus-util.h"
#include "mount.h"
#include "string-util.h"
#include "unit.h"

static const char *mount_get_what(const Mount *m) {
        if (m->from_proc_self_mountinfo && m->parameters_proc_self_mountinfo.what)
                return m->parameters_proc_self_mountinfo.what;
        if (m->from_fragment && m->parameters_fragment.what)
                return m->parameters_fragment.what;
        return NULL;
}

static const char *mount_get_options(const Mount *m) {
        if (m->from_proc_self_mountinfo && m->parameters_proc_self_mountinfo.options)
                return m->parameters_proc_self_mountinfo.options;
        if (m->from_fragment && m->parameters_fragment.options)
                return m->parameters_fragment.options;
        return NULL;
}

static const char *mount_get_fstype(const Mount *m) {
        if (m->from_proc_self_mountinfo && m->parameters_proc_self_mountinfo.fstype)
                return m->parameters_proc_self_mountinfo.fstype;
        else if (m->from_fragment && m->parameters_fragment.fstype)
                return m->parameters_fragment.fstype;
        return NULL;
}

static BUS_DEFINE_PROPERTY_GET(property_get_what, "s", Mount, mount_get_what);
static BUS_DEFINE_PROPERTY_GET(property_get_options, "s", Mount, mount_get_options);
static BUS_DEFINE_PROPERTY_GET(property_get_type, "s", Mount, mount_get_fstype);
static BUS_DEFINE_PROPERTY_GET_ENUM(property_get_result, mount_result, MountResult);

const sd_bus_vtable bus_mount_vtable[] = {
        SD_BUS_VTABLE_START(0),
        SD_BUS_PROPERTY("Where", "s", NULL, offsetof(Mount, where), SD_BUS_VTABLE_PROPERTY_CONST),
        SD_BUS_PROPERTY("What", "s", property_get_what, 0, SD_BUS_VTABLE_PROPERTY_EMITS_CHANGE),
        SD_BUS_PROPERTY("Options","s", property_get_options, 0, SD_BUS_VTABLE_PROPERTY_EMITS_CHANGE),
        SD_BUS_PROPERTY("Type", "s", property_get_type, 0, SD_BUS_VTABLE_PROPERTY_EMITS_CHANGE),
        SD_BUS_PROPERTY("TimeoutUSec", "t", bus_property_get_usec, offsetof(Mount, timeout_usec), SD_BUS_VTABLE_PROPERTY_CONST),
        SD_BUS_PROPERTY("ControlPID", "u", bus_property_get_pid, offsetof(Mount, control_pid), SD_BUS_VTABLE_PROPERTY_EMITS_CHANGE),
        SD_BUS_PROPERTY("DirectoryMode", "u", bus_property_get_mode, offsetof(Mount, directory_mode), SD_BUS_VTABLE_PROPERTY_CONST),
        SD_BUS_PROPERTY("SloppyOptions", "b", bus_property_get_bool, offsetof(Mount, sloppy_options), SD_BUS_VTABLE_PROPERTY_CONST),
        SD_BUS_PROPERTY("LazyUnmount", "b", bus_property_get_bool, offsetof(Mount, lazy_unmount), SD_BUS_VTABLE_PROPERTY_CONST),
        SD_BUS_PROPERTY("ForceUnmount", "b", bus_property_get_bool, offsetof(Mount, force_unmount), SD_BUS_VTABLE_PROPERTY_CONST),
        SD_BUS_PROPERTY("Result", "s", property_get_result, offsetof(Mount, result), SD_BUS_VTABLE_PROPERTY_EMITS_CHANGE),
        SD_BUS_PROPERTY("UID", "u", bus_property_get_uid, offsetof(Unit, ref_uid), SD_BUS_VTABLE_PROPERTY_EMITS_CHANGE),
        SD_BUS_PROPERTY("GID", "u", bus_property_get_gid, offsetof(Unit, ref_gid), SD_BUS_VTABLE_PROPERTY_EMITS_CHANGE),
        BUS_EXEC_COMMAND_VTABLE("ExecMount", offsetof(Mount, exec_command[MOUNT_EXEC_MOUNT]), SD_BUS_VTABLE_PROPERTY_EMITS_INVALIDATION),
        BUS_EXEC_COMMAND_VTABLE("ExecUnmount", offsetof(Mount, exec_command[MOUNT_EXEC_UNMOUNT]), SD_BUS_VTABLE_PROPERTY_EMITS_INVALIDATION),
        BUS_EXEC_COMMAND_VTABLE("ExecRemount", offsetof(Mount, exec_command[MOUNT_EXEC_REMOUNT]), SD_BUS_VTABLE_PROPERTY_EMITS_INVALIDATION),
        SD_BUS_VTABLE_END
};

static int bus_mount_set_transient_property(
                Mount *m,
                const char *name,
                sd_bus_message *message,
                UnitWriteFlags flags,
                sd_bus_error *error) {

        Unit *u = UNIT(m);

        assert(m);
        assert(name);
        assert(message);

        flags |= UNIT_PRIVATE;

        if (streq(name, "Where"))
                return bus_set_transient_path(u, name, &m->where, message, flags, error);

        if (streq(name, "What"))
                return bus_set_transient_string(u, name, &m->parameters_fragment.what, message, flags, error);

        if (streq(name, "Options"))
                return bus_set_transient_string(u, name, &m->parameters_fragment.options, message, flags, error);

        if (streq(name, "Type"))
                return bus_set_transient_string(u, name, &m->parameters_fragment.fstype, message, flags, error);

        if (streq(name, "TimeoutUSec"))
                return bus_set_transient_usec_fix_0(u, name, &m->timeout_usec, message, flags, error);

        if (streq(name, "DirectoryMode"))
                return bus_set_transient_mode_t(u, name, &m->directory_mode, message, flags, error);

        if (streq(name, "SloppyOptions"))
                return bus_set_transient_bool(u, name, &m->sloppy_options, message, flags, error);

        if (streq(name, "LazyUnmount"))
                return bus_set_transient_bool(u, name, &m->lazy_unmount, message, flags, error);

        if (streq(name, "ForceUnmount"))
                return bus_set_transient_bool(u, name, &m->force_unmount, message, flags, error);

        return 0;
}

int bus_mount_set_property(
                Unit *u,
                const char *name,
                sd_bus_message *message,
                UnitWriteFlags flags,
                sd_bus_error *error) {

        Mount *m = MOUNT(u);
        int r;

        assert(m);
        assert(name);
        assert(message);

        r = bus_cgroup_set_property(u, &m->cgroup_context, name, message, flags, error);
        if (r != 0)
                return r;

        if (u->transient && u->load_state == UNIT_STUB) {
                /* This is a transient unit, let's load a little more */

                r = bus_mount_set_transient_property(m, name, message, flags, error);
                if (r != 0)
                        return r;

                r = bus_exec_context_set_transient_property(u, &m->exec_context, name, message, flags, error);
                if (r != 0)
                        return r;

                r = bus_kill_context_set_transient_property(u, &m->kill_context, name, message, flags, error);
                if (r != 0)
                        return r;
        }

        return 0;
}

int bus_mount_commit_properties(Unit *u) {
        assert(u);

        unit_invalidate_cgroup_members_masks(u);
        unit_realize_cgroup(u);

        return 0;
}
