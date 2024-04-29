#!/bin/sh

log()
{
    logger -t auto-upgrade -p user.info $@
}

# Only keep the most recent 10 versions of a given log file.
rotate()
{
    DIR="$1"
    FILE="$2"

    [ $# -ne 2 ] && { log "usage: rotate directory logfile"; return 0; }

    cd "$DIR"
    [ -f $FILE ] || { log "not rotating $FILE: no such file"; return 0; }
    N=8
    while [ $N -ge 0 ]
    do
	if [ -f $FILE.$N ]
	then
	    M=$(expr $N + 1)
	    mv $FILE.$N $FILE.$M
	fi
	N=$(expr $N - 1)
    done
    mv $FILE $FILE.0

    cd "$OLDPWD"
}

log "Executing auto-upgrade.sh"
DIR='/var/run/auto-upgrade'
UPGRADE_STAMP="$DIR/last_upgrade"

[ ! -d $DIR ] && mkdir -p $DIR

# See if the system needs to be rebooted. Only do this between 2am and 5am.
HOUR=$(date +%H)
if [ $HOUR -ge 2 -a $HOUR -lt 5  -a -f /run/reboot-required ]
then
    log "Rebooting because /run/reboot-required exists"
    sleep 5
    /usr/sbin/reboot
fi

# See if we should run "apt-get update". The idea is that we'll run this
# script once per hour, but only execute apt-get if it's been more than
# 24 hours since the last time we ran it.
NOW=$(date +%s)
if [ -f $UPGRADE_STAMP ]
then
    LAST_UPGRADE=$(date -r $UPGRADE_STAMP +%s)
else
    LAST_UPGRADE=0
fi
INTERVAL=$(expr $NOW - $LAST_UPGRADE)
log "Seconds since last update: $INTERVAL"

[ $INTERVAL -le 82800 ] && exit 0

# See if we need to run "apt-get upgrade". We'll only run this if:
# 1. There are packages that need to be upgraded.
# 2. It's been more than 24 hours since we last upgraded.
# 3. XFreeRDP isn't running (we don't want to suck up the bandwidth while
#    someone is in the midst of an active remote connection.)
if [ $(grep -i xfreerdp /proc/[0-9]*/comm | wc -l) -gt 0 ]
then
    log "XfreeRDP is running. Not going to upgrade."
    exit 0
fi
log apt-get update
rotate $DIR update.log
apt-get update > $DIR/update.log 2>&1

log apt-get upgrade
rotate $DIR upgrade.log
apt-get upgrade -y --with-new-pkgs > $DIR/upgrade.log 2>&1

log apt-get autoremove
rotate $DIR autoremove.log
apt-get autoremove -y > $DIR/autoremove.log 2>&1

touch $UPGRADE_STAMP
