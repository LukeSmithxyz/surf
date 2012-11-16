#!/bin/sh
#
# See the LICENSE file for copyright and license details. 
#

xidfile="$HOME/tmp/tabbed-surf.xid"
uri=""

if [ "$#" -gt 0 ];
then
	uri="$1"
fi

runtabbed() {
	tabbed -dn tabbed-surf -r 2 surf -e '' "$uri" >"$xidfile" \
		2>/dev/null &
}

if [ ! -r "$xidfile" ];
then
	runtabbed
else
	xid=$(cat "$xidfile")
	xprop -id "$xid" 2>&1 >/dev/null
	if [ $? -gt 0 ];
	then
		runtabbed
	else
		surf -e "$xid" "$uri" 2>&1 >/dev/null &
	fi
fi

