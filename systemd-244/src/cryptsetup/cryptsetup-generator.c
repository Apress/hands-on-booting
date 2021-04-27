/* SPDX-License-Identifier: LGPL-2.1+ */

#include <errno.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/types.h>

#include "alloc-util.h"
#include "dropin.h"
#include "escape.h"
#include "fd-util.h"
#include "fileio.h"
#include "fstab-util.h"
#include "generator.h"
#include "hashmap.h"
#include "id128-util.h"
#include "log.h"
#include "mkdir.h"
#include "parse-util.h"
#include "path-util.h"
#include "proc-cmdline.h"
#include "specifier.h"
#include "string-util.h"
#include "strv.h"
#include "unit-name.h"
#include "util.h"

typedef struct crypto_device {
        char *uuid;
        char *keyfile;
        char *keydev;
        char *name;
        char *options;
        bool create;
} crypto_device;

static const char *arg_dest = NULL;
static bool arg_enabled = true;
static bool arg_read_crypttab = true;
static const char *arg_crypttab = NULL;
static const char *arg_runtime_directory = NULL;
static bool arg_whitelist = false;
static Hashmap *arg_disks = NULL;
static char *arg_default_options = NULL;
static char *arg_default_keyfile = NULL;

STATIC_DESTRUCTOR_REGISTER(arg_disks, hashmap_freep);
STATIC_DESTRUCTOR_REGISTER(arg_default_options, freep);
STATIC_DESTRUCTOR_REGISTER(arg_default_keyfile, freep);

static int split_keyspec(const char *keyspec, char **ret_keyfile, char **ret_keydev) {
        _cleanup_free_ char *keyfile = NULL, *keydev = NULL;
        const char *c;

        assert(ret_keyfile);
        assert(ret_keydev);

        if (!keyspec) {
                *ret_keyfile = *ret_keydev = NULL;
                return 0;
        }

        c = strrchr(keyspec, ':');
        if (c) {
                /* The keydev part has to be either an absolute path to device node (/dev/something,
                 * /dev/foo/something, or even possibly /dev/foo/something:part), or a fstab device
                 * specification starting with LABEL= or similar. The keyfile part has the same syntax.
                 *
                 * Let's try to guess if the second part looks like a keydev specification, or just part of a
                 * filename with a colon. fstab_node_to_udev_node() will convert the fstab device syntax to
                 * an absolute path. If we didn't get an absolute path, assume that it is just part of the
                 * first keyfile argument. */

                keydev = fstab_node_to_udev_node(c + 1);
                if (!keydev)
                        return log_oom();

                if (path_is_absolute(keydev))
                        keyfile = strndup(keyspec, c-keyspec);
                else {
                        log_debug("Keyspec argument contains a colon, but \"%s\" doesn't look like a device specification.\n"
                                  "Assuming that \"%s\" is a single device specification.",
                                  c + 1, keyspec);
                        keydev = mfree(keydev);
                        c = NULL;
                }
        }

        if (!c)
                /* No keydev specified */
                keyfile = strdup(keyspec);

        if (!keyfile)
                return log_oom();

        *ret_keyfile = TAKE_PTR(keyfile);
        *ret_keydev = TAKE_PTR(keydev);

        return 0;
}

static int generate_keydev_mount(const char *name, const char *keydev, const char *keydev_timeout, bool canfail, char **unit, char **mount) {
        _cleanup_free_ char *u = NULL, *where = NULL, *name_escaped = NULL, *device_unit = NULL;
        _cleanup_fclose_ FILE *f = NULL;
        int r;
        usec_t timeout_us;

        assert(name);
        assert(keydev);
        assert(unit);
        assert(mount);

        r = mkdir_parents(arg_runtime_directory, 0755);
        if (r < 0)
                return r;

        r = mkdir(arg_runtime_directory, 0700);
        if (r < 0 && errno != EEXIST)
                return -errno;

        name_escaped = cescape(name);
        if (!name_escaped)
                return -ENOMEM;

        where = strjoin(arg_runtime_directory, "/keydev-", name_escaped);
        if (!where)
                return -ENOMEM;

        r = mkdir(where, 0700);
        if (r < 0 && errno != EEXIST)
                return -errno;

        r = unit_name_from_path(where, ".mount", &u);
        if (r < 0)
                return r;

        r = generator_open_unit_file(arg_dest, NULL, u, &f);
        if (r < 0)
                return r;

        fprintf(f,
                "[Unit]\n"
                "DefaultDependencies=no\n\n"
                "[Mount]\n"
                "What=%s\n"
                "Where=%s\n"
                "Options=ro%s\n", keydev, where, canfail ? ",nofail" : "");

        if (keydev_timeout) {
                r = parse_sec_fix_0(keydev_timeout, &timeout_us);
                if (r >= 0) {
                        r = unit_name_from_path(keydev, ".device", &device_unit);
                        if (r < 0)
                                return log_error_errno(r, "Failed to generate unit name: %m");

                        r = write_drop_in_format(arg_dest, device_unit, 90, "device-timeout",
                                "# Automatically generated by systemd-cryptsetup-generator \n\n"
                                "[Unit]\nJobRunningTimeoutSec=%s", keydev_timeout);
                        if (r < 0)
                                return log_error_errno(r, "Failed to write device drop-in: %m");

                } else
                        log_warning_errno(r, "Failed to parse %s, ignoring: %m", keydev_timeout);

        }

        r = fflush_and_check(f);
        if (r < 0)
                return r;

        *unit = TAKE_PTR(u);
        *mount = TAKE_PTR(where);

        return 0;
}

static int print_dependencies(FILE *f, const char* device_path) {
        int r;

        if (STR_IN_SET(device_path, "-", "none"))
                /* None, nothing to do */
                return 0;

        if (PATH_IN_SET(device_path, "/dev/urandom", "/dev/random", "/dev/hw_random")) {
                /* RNG device, add random dep */
                fputs("After=systemd-random-seed.service\n", f);
                return 0;
        }

        _cleanup_free_ char *udev_node = fstab_node_to_udev_node(device_path);
        if (!udev_node)
                return log_oom();

        if (path_equal(udev_node, "/dev/null"))
                return 0;

        if (path_startswith(udev_node, "/dev/")) {
                /* We are dealing with a block device, add dependency for correspoding unit */
                _cleanup_free_ char *unit = NULL;

                r = unit_name_from_path(udev_node, ".device", &unit);
                if (r < 0)
                        return log_error_errno(r, "Failed to generate unit name: %m");

                fprintf(f, "After=%1$s\nRequires=%1$s\n", unit);
        } else {
                /* Regular file, add mount dependency */
                _cleanup_free_ char *escaped_path = specifier_escape(device_path);
                if (!escaped_path)
                        return log_oom();

                fprintf(f, "RequiresMountsFor=%s\n", escaped_path);
        }

        return 0;
}

static int create_disk(
                const char *name,
                const char *device,
                const char *password,
                const char *keydev,
                const char *options) {

        _cleanup_free_ char *n = NULL, *d = NULL, *u = NULL, *e = NULL,
                *keydev_mount = NULL, *keyfile_timeout_value = NULL, *password_escaped = NULL,
                *filtered = NULL, *u_escaped = NULL, *filtered_escaped = NULL, *name_escaped = NULL, *header_path = NULL;
        _cleanup_fclose_ FILE *f = NULL;
        const char *dmname;
        bool noauto, nofail, tmp, swap, netdev;
        int r, detached_header, keyfile_can_timeout;

        assert(name);
        assert(device);

        noauto = fstab_test_yes_no_option(options, "noauto\0" "auto\0");
        nofail = fstab_test_yes_no_option(options, "nofail\0" "fail\0");
        tmp = fstab_test_option(options, "tmp\0");
        swap = fstab_test_option(options, "swap\0");
        netdev = fstab_test_option(options, "_netdev\0");

        keyfile_can_timeout = fstab_filter_options(options, "keyfile-timeout\0", NULL, &keyfile_timeout_value, NULL);
        if (keyfile_can_timeout < 0)
                return log_error_errno(keyfile_can_timeout, "Failed to parse keyfile-timeout= option value: %m");

        detached_header = fstab_filter_options(options, "header\0", NULL, &header_path, NULL);
        if (detached_header < 0)
                return log_error_errno(detached_header, "Failed to parse header= option value: %m");

        if (tmp && swap)
                return log_error_errno(SYNTHETIC_ERRNO(EINVAL),
                                       "Device '%s' cannot be both 'tmp' and 'swap'. Ignoring.",
                                       name);

        name_escaped = specifier_escape(name);
        if (!name_escaped)
                return log_oom();

        e = unit_name_escape(name);
        if (!e)
                return log_oom();

        u = fstab_node_to_udev_node(device);
        if (!u)
                return log_oom();

        r = unit_name_build("systemd-cryptsetup", e, ".service", &n);
        if (r < 0)
                return log_error_errno(r, "Failed to generate unit name: %m");

        u_escaped = specifier_escape(u);
        if (!u_escaped)
                return log_oom();

        r = unit_name_from_path(u, ".device", &d);
        if (r < 0)
                return log_error_errno(r, "Failed to generate unit name: %m");

        if (keydev && !password)
                return log_error_errno(SYNTHETIC_ERRNO(EINVAL),
                                       "Key device is specified, but path to the password file is missing.");

        r = generator_open_unit_file(arg_dest, NULL, n, &f);
        if (r < 0)
                return r;

        fprintf(f,
                "[Unit]\n"
                "Description=Cryptography Setup for %%I\n"
                "Documentation=man:crypttab(5) man:systemd-cryptsetup-generator(8) man:systemd-cryptsetup@.service(8)\n"
                "SourcePath=%s\n"
                "DefaultDependencies=no\n"
                "Conflicts=umount.target\n"
                "IgnoreOnIsolate=true\n"
                "After=%s\n",
                arg_crypttab,
                netdev ? "remote-fs-pre.target" : "cryptsetup-pre.target");

        if (password) {
                password_escaped = specifier_escape(password);
                if (!password_escaped)
                        return log_oom();
        }

        if (keydev) {
                _cleanup_free_ char *unit = NULL, *p = NULL;

                r = generate_keydev_mount(name, keydev, keyfile_timeout_value, keyfile_can_timeout > 0, &unit, &keydev_mount);
                if (r < 0)
                        return log_error_errno(r, "Failed to generate keydev mount unit: %m");

                p = path_join(keydev_mount, password_escaped);
                if (!p)
                        return log_oom();

                free_and_replace(password_escaped, p);

                fprintf(f, "After=%s\n", unit);
                if (keyfile_can_timeout > 0)
                        fprintf(f, "Wants=%s\n", unit);
                else
                        fprintf(f, "Requires=%s\n", unit);
        }

        if (!nofail)
                fprintf(f,
                        "Before=%s\n",
                        netdev ? "remote-cryptsetup.target" : "cryptsetup.target");

        if (password && !keydev) {
                r = print_dependencies(f, password);
                if (r < 0)
                        return r;
        }

        /* Check if a header option was specified */
        if (detached_header > 0) {
                r = print_dependencies(f, header_path);
                if (r < 0)
                        return r;
        }

        if (path_startswith(u, "/dev/")) {
                fprintf(f,
                        "BindsTo=%s\n"
                        "After=%s\n"
                        "Before=umount.target\n",
                        d, d);

                if (swap)
                        fputs("Before=dev-mapper-%i.swap\n",
                              f);
        } else
                /* For loopback devices, add systemd-tmpfiles-setup-dev.service
                   dependency to ensure that loopback support is available in
                   the kernel (/dev/loop-control needs to exist) */
                fprintf(f,
                        "RequiresMountsFor=%s\n"
                        "Requires=systemd-tmpfiles-setup-dev.service\n"
                        "After=systemd-tmpfiles-setup-dev.service\n",
                        u_escaped);

        r = generator_write_timeouts(arg_dest, device, name, options, &filtered);
        if (r < 0)
                return r;

        if (filtered) {
                filtered_escaped = specifier_escape(filtered);
                if (!filtered_escaped)
                        return log_oom();
        }

        fprintf(f,
                "\n[Service]\n"
                "Type=oneshot\n"
                "RemainAfterExit=yes\n"
                "TimeoutSec=0\n" /* the binary handles timeouts anyway */
                "KeyringMode=shared\n" /* make sure we can share cached keys among instances */
                "OOMScoreAdjust=500\n" /* unlocking can allocate a lot of memory if Argon2 is used */
                "ExecStart=" SYSTEMD_CRYPTSETUP_PATH " attach '%s' '%s' '%s' '%s'\n"
                "ExecStop=" SYSTEMD_CRYPTSETUP_PATH " detach '%s'\n",
                name_escaped, u_escaped, strempty(password_escaped), strempty(filtered_escaped),
                name_escaped);

        if (tmp)
                fprintf(f,
                        "ExecStartPost=/sbin/mke2fs '/dev/mapper/%s'\n",
                        name_escaped);

        if (swap)
                fprintf(f,
                        "ExecStartPost=/sbin/mkswap '/dev/mapper/%s'\n",
                        name_escaped);

        if (keydev)
                fprintf(f,
                        "ExecStartPost=-" UMOUNT_PATH " %s\n\n",
                        keydev_mount);

        r = fflush_and_check(f);
        if (r < 0)
                return log_error_errno(r, "Failed to write unit file %s: %m", n);

        if (!noauto) {
                r = generator_add_symlink(arg_dest,
                                          netdev ? "remote-cryptsetup.target" : "cryptsetup.target",
                                          nofail ? "wants" : "requires", n);
                if (r < 0)
                        return r;
        }

        dmname = strjoina("dev-mapper-", e, ".device");
        r = generator_add_symlink(arg_dest, dmname, "requires", n);
        if (r < 0)
                return r;

        if (!noauto && !nofail) {
                r = write_drop_in(arg_dest, dmname, 90, "device-timeout",
                                  "# Automatically generated by systemd-cryptsetup-generator \n\n"
                                  "[Unit]\nJobTimeoutSec=0");
                if (r < 0)
                        return log_error_errno(r, "Failed to write device drop-in: %m");
        }

        return 0;
}

static crypto_device* crypt_device_free(crypto_device *d) {
        if (!d)
                return NULL;

        free(d->uuid);
        free(d->keyfile);
        free(d->keydev);
        free(d->name);
        free(d->options);
        return mfree(d);
}

static crypto_device *get_crypto_device(const char *uuid) {
        int r;
        crypto_device *d;

        assert(uuid);

        d = hashmap_get(arg_disks, uuid);
        if (!d) {
                d = new0(struct crypto_device, 1);
                if (!d)
                        return NULL;

                d->uuid = strdup(uuid);
                if (!d->uuid)
                        return mfree(d);

                r = hashmap_put(arg_disks, d->uuid, d);
                if (r < 0) {
                        free(d->uuid);
                        return mfree(d);
                }
        }

        return d;
}

static int parse_proc_cmdline_item(const char *key, const char *value, void *data) {
        _cleanup_free_ char *uuid = NULL, *uuid_value = NULL;
        crypto_device *d;
        int r;

        if (streq(key, "luks")) {

                r = value ? parse_boolean(value) : 1;
                if (r < 0)
                        log_warning("Failed to parse luks= kernel command line switch %s. Ignoring.", value);
                else
                        arg_enabled = r;

        } else if (streq(key, "luks.crypttab")) {

                r = value ? parse_boolean(value) : 1;
                if (r < 0)
                        log_warning("Failed to parse luks.crypttab= kernel command line switch %s. Ignoring.", value);
                else
                        arg_read_crypttab = r;

        } else if (streq(key, "luks.uuid")) {

                if (proc_cmdline_value_missing(key, value))
                        return 0;

                d = get_crypto_device(startswith(value, "luks-") ? value+5 : value);
                if (!d)
                        return log_oom();

                d->create = arg_whitelist = true;

        } else if (streq(key, "luks.options")) {

                if (proc_cmdline_value_missing(key, value))
                        return 0;

                r = sscanf(value, "%m[0-9a-fA-F-]=%ms", &uuid, &uuid_value);
                if (r == 2) {
                        d = get_crypto_device(uuid);
                        if (!d)
                                return log_oom();

                        free_and_replace(d->options, uuid_value);
                } else if (free_and_strdup(&arg_default_options, value) < 0)
                        return log_oom();

        } else if (streq(key, "luks.key")) {
                size_t n;
                _cleanup_free_ char *keyfile = NULL, *keydev = NULL;
                const char *keyspec;

                if (proc_cmdline_value_missing(key, value))
                        return 0;

                n = strspn(value, LETTERS DIGITS "-");
                if (value[n] != '=') {
                        if (free_and_strdup(&arg_default_keyfile, value) < 0)
                                 return log_oom();
                        return 0;
                }

                uuid = strndup(value, n);
                if (!uuid)
                        return log_oom();

                if (!id128_is_valid(uuid)) {
                        log_warning("Failed to parse luks.key= kernel command line switch. UUID is invalid, ignoring.");
                        return 0;
                }

                d = get_crypto_device(uuid);
                if (!d)
                        return log_oom();

                keyspec = value + n + 1;
                r = split_keyspec(keyspec, &keyfile, &keydev);
                if (r < 0)
                        return r;

                free_and_replace(d->keyfile, keyfile);
                free_and_replace(d->keydev, keydev);

        } else if (streq(key, "luks.name")) {

                if (proc_cmdline_value_missing(key, value))
                        return 0;

                r = sscanf(value, "%m[0-9a-fA-F-]=%ms", &uuid, &uuid_value);
                if (r == 2) {
                        d = get_crypto_device(uuid);
                        if (!d)
                                return log_oom();

                        d->create = arg_whitelist = true;

                        free_and_replace(d->name, uuid_value);
                } else
                        log_warning("Failed to parse luks name switch %s. Ignoring.", value);
        }

        return 0;
}

static int add_crypttab_devices(void) {
        _cleanup_fclose_ FILE *f = NULL;
        unsigned crypttab_line = 0;
        struct stat st;
        int r;

        if (!arg_read_crypttab)
                return 0;

        r = fopen_unlocked(arg_crypttab, "re", &f);
        if (r < 0) {
                if (errno != ENOENT)
                        log_error_errno(errno, "Failed to open %s: %m", arg_crypttab);
                return 0;
        }

        if (fstat(fileno(f), &st) < 0) {
                log_error_errno(errno, "Failed to stat %s: %m", arg_crypttab);
                return 0;
        }

        for (;;) {
                _cleanup_free_ char *line = NULL, *name = NULL, *device = NULL, *keyspec = NULL, *options = NULL, *keyfile = NULL, *keydev = NULL;
                crypto_device *d = NULL;
                char *l, *uuid;
                int k;

                r = read_line(f, LONG_LINE_MAX, &line);
                if (r < 0)
                        return log_error_errno(r, "Failed to read %s: %m", arg_crypttab);
                if (r == 0)
                        break;

                crypttab_line++;

                l = strstrip(line);
                if (IN_SET(l[0], 0, '#'))
                        continue;

                k = sscanf(l, "%ms %ms %ms %ms", &name, &device, &keyspec, &options);
                if (k < 2 || k > 4) {
                        log_error("Failed to parse %s:%u, ignoring.", arg_crypttab, crypttab_line);
                        continue;
                }

                uuid = startswith(device, "UUID=");
                if (!uuid)
                        uuid = path_startswith(device, "/dev/disk/by-uuid/");
                if (!uuid)
                        uuid = startswith(name, "luks-");
                if (uuid)
                        d = hashmap_get(arg_disks, uuid);

                if (arg_whitelist && !d) {
                        log_info("Not creating device '%s' because it was not specified on the kernel command line.", name);
                        continue;
                }

                r = split_keyspec(keyspec, &keyfile, &keydev);
                if (r < 0)
                        return r;

                r = create_disk(name, device, keyfile, keydev, (d && d->options) ? d->options : options);
                if (r < 0)
                        return r;

                if (d)
                        d->create = false;
        }

        return 0;
}

static int add_proc_cmdline_devices(void) {
        int r;
        Iterator i;
        crypto_device *d;

        HASHMAP_FOREACH(d, arg_disks, i) {
                const char *options;
                _cleanup_free_ char *device = NULL;

                if (!d->create)
                        continue;

                if (!d->name) {
                        d->name = strjoin("luks-", d->uuid);
                        if (!d->name)
                                return log_oom();
                }

                device = strjoin("UUID=", d->uuid);
                if (!device)
                        return log_oom();

                if (d->options)
                        options = d->options;
                else if (arg_default_options)
                        options = arg_default_options;
                else
                        options = "timeout=0";

                r = create_disk(d->name, device, d->keyfile ?: arg_default_keyfile, d->keydev, options);
                if (r < 0)
                        return r;
        }

        return 0;
}

DEFINE_PRIVATE_HASH_OPS_WITH_VALUE_DESTRUCTOR(crypt_device_hash_ops, char, string_hash_func, string_compare_func,
                                              crypto_device, crypt_device_free);

static int run(const char *dest, const char *dest_early, const char *dest_late) {
        int r;

        assert_se(arg_dest = dest);

        arg_crypttab = getenv("SYSTEMD_CRYPTTAB") ?: "/etc/crypttab";
        arg_runtime_directory = getenv("RUNTIME_DIRECTORY") ?: "/run/systemd/cryptsetup";

        arg_disks = hashmap_new(&crypt_device_hash_ops);
        if (!arg_disks)
                return log_oom();

        r = proc_cmdline_parse(parse_proc_cmdline_item, NULL, PROC_CMDLINE_STRIP_RD_PREFIX);
        if (r < 0)
                return log_warning_errno(r, "Failed to parse kernel command line: %m");

        if (!arg_enabled)
                return 0;

        r = add_crypttab_devices();
        if (r < 0)
                return r;

        r = add_proc_cmdline_devices();
        if (r < 0)
                return r;

        return 0;
}

DEFINE_MAIN_GENERATOR_FUNCTION(run);
