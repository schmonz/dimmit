#!/bin/sh
#
# PROVIDE: dimmitd
# REQUIRE: DAEMON
# BEFORE: LOGIN

$_rc_subr_loaded . /etc/rc.subr

name="dimmitd"
rcvar=$name
command="@CMAKE_INSTALL_FULL_SBINDIR@/${name}"
pidfile="/var/run/${name}.pid"

load_rc_config $name
run_rc_command "$1"
