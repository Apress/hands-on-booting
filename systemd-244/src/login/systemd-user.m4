# This file is part of systemd.
#
# Used by systemd --user instances.

account required pam_unix.so
m4_ifdef(`HAVE_SELINUX',
session required pam_selinux.so close
session required pam_selinux.so nottys open
)m4_dnl
session required pam_loginuid.so
session optional pam_keyinit.so force revoke
session optional pam_systemd.so
