#! /bin/bash

set -e
set -x

rm -fr /tmp/{z,Z}
mkdir  /tmp/{z,Z}

#
# 'z'
#
mkdir /tmp/z/d{1,2}
touch /tmp/z/f1 /tmp/z/d1/f11 /tmp/z/d2/f21

systemd-tmpfiles --create - <<EOF
z     /tmp/z/f1    0755 daemon daemon - -
z     /tmp/z/d1    0755 daemon daemon - -
EOF

test $(stat -c %U:%G /tmp/z/f1) = "daemon:daemon"
test $(stat -c %U:%G /tmp/z/d1) = "daemon:daemon"
test $(stat -c %U:%G /tmp/z/d1/f11) = "root:root"

systemd-tmpfiles --create - <<EOF
z     /tmp/z/d2/*    0755 daemon daemon - -
EOF

test $(stat -c %U:%G /tmp/z/d2/f21) = "daemon:daemon"

#
# 'Z'
#
mkdir /tmp/Z/d1 /tmp/Z/d1/d11
touch /tmp/Z/f1 /tmp/Z/d1/f11 /tmp/Z/d1/d11/f111

systemd-tmpfiles --create - <<EOF
Z     /tmp/Z/f1    0755 daemon daemon - -
Z     /tmp/Z/d1    0755 daemon daemon - -
EOF

test $(stat -c %U:%G /tmp/Z/f1) = "daemon:daemon"
test $(stat -c %U:%G /tmp/Z/d1) = "daemon:daemon"
test $(stat -c %U:%G /tmp/Z/d1/d11) = "daemon:daemon"
test $(stat -c %U:%G /tmp/Z/d1/f11) = "daemon:daemon"
test $(stat -c %U:%G /tmp/Z/d1/d11/f111) = "daemon:daemon"
