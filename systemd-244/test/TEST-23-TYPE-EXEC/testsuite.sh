#!/bin/bash
set -ex
set -o pipefail

systemd-analyze log-level debug
systemd-analyze log-target console

# Create a binary for which execve() will fail
touch /tmp/brokenbinary
chmod +x /tmp/brokenbinary

# These three commands should succeed.
systemd-run --unit=one -p Type=simple /bin/sleep infinity
systemd-run --unit=two -p Type=simple -p User=idontexist /bin/sleep infinity
systemd-run --unit=three -p Type=simple /tmp/brokenbinary

# And now, do the same with Type=exec, where the latter two should fail
systemd-run --unit=four -p Type=exec /bin/sleep infinity
! systemd-run --unit=five -p Type=exec -p User=idontexist /bin/sleep infinity
! systemd-run --unit=six -p Type=exec /tmp/brokenbinary

systemd-run --unit=seven -p KillSignal=SIGTERM -p RestartKillSignal=SIGINT -p Type=exec /bin/sleep infinity
# Both TERM and SIGINT happen to have the same number on all architectures
test $(systemctl show --value -p KillSignal seven.service) -eq 15
test $(systemctl show --value -p RestartKillSignal seven.service) -eq 2

systemctl restart seven.service
systemctl stop seven.service

systemd-analyze log-level info

echo OK > /testok

exit 0
