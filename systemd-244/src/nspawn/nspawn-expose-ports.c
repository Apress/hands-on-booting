/* SPDX-License-Identifier: LGPL-2.1+ */

#include "sd-netlink.h"

#include "alloc-util.h"
#include "fd-util.h"
#include "firewall-util.h"
#include "in-addr-util.h"
#include "local-addresses.h"
#include "netlink-util.h"
#include "nspawn-expose-ports.h"
#include "parse-util.h"
#include "socket-util.h"
#include "string-util.h"
#include "util.h"

int expose_port_parse(ExposePort **l, const char *s) {

        const char *split, *e;
        uint16_t container_port, host_port;
        int protocol;
        ExposePort *p;
        int r;

        assert(l);
        assert(s);

        if ((e = startswith(s, "tcp:")))
                protocol = IPPROTO_TCP;
        else if ((e = startswith(s, "udp:")))
                protocol = IPPROTO_UDP;
        else {
                e = s;
                protocol = IPPROTO_TCP;
        }

        split = strchr(e, ':');
        if (split) {
                char v[split - e + 1];

                memcpy(v, e, split - e);
                v[split - e] = 0;

                r = parse_ip_port(v, &host_port);
                if (r < 0)
                        return -EINVAL;

                r = parse_ip_port(split + 1, &container_port);
        } else {
                r = parse_ip_port(e, &container_port);
                host_port = container_port;
        }

        if (r < 0)
                return -EINVAL;

        LIST_FOREACH(ports, p, *l)
                if (p->protocol == protocol && p->host_port == host_port)
                        return -EEXIST;

        p = new(ExposePort, 1);
        if (!p)
                return -ENOMEM;

        p->protocol = protocol;
        p->host_port = host_port;
        p->container_port = container_port;

        LIST_PREPEND(ports, *l, p);

        return 0;
}

void expose_port_free_all(ExposePort *p) {

        while (p) {
                ExposePort *q = p;
                LIST_REMOVE(ports, p, q);
                free(q);
        }
}

int expose_port_flush(ExposePort* l, union in_addr_union *exposed) {
        ExposePort *p;
        int r, af = AF_INET;

        assert(exposed);

        if (!l)
                return 0;

        if (in_addr_is_null(af, exposed))
                return 0;

        log_debug("Lost IP address.");

        LIST_FOREACH(ports, p, l) {
                r = fw_add_local_dnat(false,
                                      af,
                                      p->protocol,
                                      NULL,
                                      NULL, 0,
                                      NULL, 0,
                                      p->host_port,
                                      exposed,
                                      p->container_port,
                                      NULL);
                if (r < 0)
                        log_warning_errno(r, "Failed to modify firewall: %m");
        }

        *exposed = IN_ADDR_NULL;
        return 0;
}

int expose_port_execute(sd_netlink *rtnl, ExposePort *l, union in_addr_union *exposed) {
        _cleanup_free_ struct local_address *addresses = NULL;
        _cleanup_free_ char *pretty = NULL;
        union in_addr_union new_exposed;
        ExposePort *p;
        bool add;
        int af = AF_INET, r;

        assert(exposed);

        /* Invoked each time an address is added or removed inside the
         * container */

        if (!l)
                return 0;

        r = local_addresses(rtnl, 0, af, &addresses);
        if (r < 0)
                return log_error_errno(r, "Failed to enumerate local addresses: %m");

        add = r > 0 &&
                addresses[0].family == af &&
                addresses[0].scope < RT_SCOPE_LINK;

        if (!add)
                return expose_port_flush(l, exposed);

        new_exposed = addresses[0].address;
        if (in_addr_equal(af, exposed, &new_exposed))
                return 0;

        in_addr_to_string(af, &new_exposed, &pretty);
        log_debug("New container IP is %s.", strna(pretty));

        LIST_FOREACH(ports, p, l) {

                r = fw_add_local_dnat(true,
                                      af,
                                      p->protocol,
                                      NULL,
                                      NULL, 0,
                                      NULL, 0,
                                      p->host_port,
                                      &new_exposed,
                                      p->container_port,
                                      in_addr_is_null(af, exposed) ? NULL : exposed);
                if (r < 0)
                        log_warning_errno(r, "Failed to modify firewall: %m");
        }

        *exposed = new_exposed;
        return 0;
}

int expose_port_send_rtnl(int send_fd) {
        _cleanup_close_ int fd = -1;
        int r;

        assert(send_fd >= 0);

        fd = socket(PF_NETLINK, SOCK_RAW|SOCK_CLOEXEC|SOCK_NONBLOCK, NETLINK_ROUTE);
        if (fd < 0)
                return log_error_errno(errno, "Failed to allocate container netlink: %m");

        /* Store away the fd in the socket, so that it stays open as
         * long as we run the child */
        r = send_one_fd(send_fd, fd, 0);
        if (r < 0)
                return log_error_errno(r, "Failed to send netlink fd: %m");

        return 0;
}

int expose_port_watch_rtnl(
                sd_event *event,
                int recv_fd,
                sd_netlink_message_handler_t handler,
                union in_addr_union *exposed,
                sd_netlink **ret) {
        _cleanup_(sd_netlink_unrefp) sd_netlink *rtnl = NULL;
        int fd, r;

        assert(event);
        assert(recv_fd >= 0);
        assert(ret);

        fd = receive_one_fd(recv_fd, 0);
        if (fd < 0)
                return log_error_errno(fd, "Failed to recv netlink fd: %m");

        r = sd_netlink_open_fd(&rtnl, fd);
        if (r < 0) {
                safe_close(fd);
                return log_error_errno(r, "Failed to create rtnl object: %m");
        }

        r = sd_netlink_add_match(rtnl, NULL, RTM_NEWADDR, handler, NULL, exposed, "nspawn-NEWADDR");
        if (r < 0)
                return log_error_errno(r, "Failed to subscribe to RTM_NEWADDR messages: %m");

        r = sd_netlink_add_match(rtnl, NULL, RTM_DELADDR, handler, NULL, exposed, "nspawn-DELADDR");
        if (r < 0)
                return log_error_errno(r, "Failed to subscribe to RTM_DELADDR messages: %m");

        r = sd_netlink_attach_event(rtnl, event, 0);
        if (r < 0)
                return log_error_errno(r, "Failed to add to event loop: %m");

        *ret = TAKE_PTR(rtnl);

        return 0;
}
