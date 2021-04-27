/* SPDX-License-Identifier: LGPL-2.1+ */

#include <net/ethernet.h>
#include <linux/nl80211.h>

#include "sd-bus.h"

#include "bus-util.h"
#include "netlink-internal.h"
#include "netlink-util.h"
#include "networkd-link.h"
#include "networkd-manager.h"
#include "networkd-wifi.h"
#include "string-util.h"
#include "wifi-util.h"

int wifi_get_info(Link *link) {
        const char *type;
        int r, s = 0;

        assert(link);

        if (!link->sd_device)
                return 0;

        r = sd_device_get_devtype(link->sd_device, &type);
        if (r == -ENOENT)
                return 0;
        else if (r < 0)
                return r;

        if (!streq(type, "wlan"))
                return 0;

        _cleanup_free_ char *ssid = NULL;
        r = wifi_get_interface(link->manager->genl, link->ifindex, &link->wlan_iftype, &ssid);
        if (r < 0)
                return r;
        if (r > 0 && streq_ptr(link->ssid, ssid))
                r = 0;
        free_and_replace(link->ssid, ssid);

        if (link->wlan_iftype == NL80211_IFTYPE_STATION) {
                struct ether_addr old_bssid = link->bssid;
                s = wifi_get_station(link->manager->genl, link->ifindex, &link->bssid);
                if (s < 0)
                        return s;
                if (s > 0 && memcmp(&old_bssid, &link->bssid, sizeof old_bssid) == 0)
                        s = 0;
        }

        if (r > 0 || s > 0) {
                char buf[ETHER_ADDR_TO_STRING_MAX];

                if (link->wlan_iftype == NL80211_IFTYPE_STATION && link->ssid)
                        log_link_info(link, "Connected WiFi access point: %s (%s)",
                                      link->ssid, ether_addr_to_string(&link->bssid, buf));
                return 1;
        }
        return 0;
}
