/* SPDX-License-Identifier: LGPL-2.1+ */

#include <net/ethernet.h>
#include <linux/nl80211.h>

#include "sd-bus.h"

#include "log.h"
#include "netlink-util.h"
#include "wifi-util.h"

int wifi_get_interface(sd_netlink *genl, int ifindex, enum nl80211_iftype *iftype, char **ssid) {
        _cleanup_(sd_netlink_message_unrefp) sd_netlink_message *m = NULL, *reply = NULL;
        sd_genl_family family;
        int r;

        r = sd_genl_message_new(genl, SD_GENL_NL80211, NL80211_CMD_GET_INTERFACE, &m);
        if (r < 0)
                return log_debug_errno(r, "Failed to create generic netlink message: %m");

        r = sd_netlink_message_append_u32(m, NL80211_ATTR_IFINDEX, ifindex);
        if (r < 0)
                return log_debug_errno(r, "Could not append NL80211_ATTR_IFINDEX attribute: %m");

        r = sd_netlink_call(genl, m, 0, &reply);
        if (r < 0)
                return log_debug_errno(r, "Failed to request information about wifi interface %d: %m", ifindex);
        if (!reply)
                return 0;

        r = sd_netlink_message_get_errno(reply);
        if (r < 0)
                return log_debug_errno(r, "Failed to get information about wifi interface %d: %m", ifindex);

        r = sd_genl_message_get_family(genl, reply, &family);
        if (r < 0)
                return log_debug_errno(r, "Failed to determine genl family: %m");
        if (family != SD_GENL_NL80211) {
                log_debug("Received message of unexpected genl family %u, ignoring.", family);
                return 0;
        }

        if (iftype) {
                uint32_t t;

                r = sd_netlink_message_read_u32(reply, NL80211_ATTR_IFTYPE, &t);
                if (r < 0)
                        return log_debug_errno(r, "Failed to get NL80211_ATTR_IFTYPE attribute: %m");
                *iftype = t;
        }

        if (ssid) {
                r = sd_netlink_message_read_string_strdup(reply, NL80211_ATTR_SSID, ssid);
                if (r < 0 && r != -ENODATA)
                        return log_debug_errno(r, "Failed to get NL80211_ATTR_SSID attribute: %m");
        }

        return r == -ENODATA ? 0 : 1;
}

int wifi_get_station(sd_netlink *genl, int ifindex, struct ether_addr *bssid) {
        _cleanup_(sd_netlink_message_unrefp) sd_netlink_message *m = NULL, *reply = NULL;
        sd_genl_family family;
        int r;

        r = sd_genl_message_new(genl, SD_GENL_NL80211, NL80211_CMD_GET_STATION, &m);
        if (r < 0)
                return log_debug_errno(r, "Failed to create generic netlink message: %m");

        r = sd_netlink_message_set_flags(m, NLM_F_REQUEST | NLM_F_ACK | NLM_F_DUMP);
        if (r < 0)
                return log_debug_errno(r, "Failed to set dump flag: %m");

        r = sd_netlink_message_append_u32(m, NL80211_ATTR_IFINDEX, ifindex);
        if (r < 0)
                return log_debug_errno(r, "Could not append NL80211_ATTR_IFINDEX attribute: %m");

        r = sd_netlink_call(genl, m, 0, &reply);
        if (r < 0)
                return log_debug_errno(r, "Failed to request information about wifi station: %m");
        if (!reply)
                return 0;

        r = sd_netlink_message_get_errno(reply);
        if (r < 0)
                return log_debug_errno(r, "Failed to get information about wifi station: %m");

        r = sd_genl_message_get_family(genl, reply, &family);
        if (r < 0)
                return log_debug_errno(r, "Failed to determine genl family: %m");
        if (family != SD_GENL_NL80211) {
                log_debug("Received message of unexpected genl family %u, ignoring.", family);
                return 0;
        }

        r = sd_netlink_message_read_ether_addr(reply, NL80211_ATTR_MAC, bssid);
        if (r < 0 && r != -ENODATA)
                return log_debug_errno(r, "Failed to get NL80211_ATTR_MAC attribute: %m");

        return r == -ENODATA ? 0 : 1;
}
