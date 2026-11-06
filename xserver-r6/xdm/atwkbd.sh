#!/sbin/sh
# atwkbd - the keyboard and mouse back to X on the ATW800/2, after
# Ctrl+Alt+F10 there lent them to the console on the TT's own screen.
# Logging out of the console does the same. Xatw only looks at the time
# on the marker; it is everyone's to touch.
umask 0
touch /tmp/.atwkbd 2>/dev/null || { echo "atwkbd: cannot touch /tmp/.atwkbd" >&2; exit 1; }
