#!/bin/sh

log()
{
    logger -t auto-upgrade -p user.info $@
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
apt-get update > $DIR/update.log 2>&1

log apt-get upgrade
apt-get upgrade -y --with-new-pkgs > $DIR/upgrade.log 2>&1

log apt-get autoremove
apt-get autoremove -y > $DIR/autoremove.log 2>&1

touch $UPGRADE_STAMP
