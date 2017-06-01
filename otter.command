#!/bin/bash
cd -- "$(dirname "$0")"

TTYALL="$(ls /dev/tty.usbmodem*)"

if [ "$?" = "0" ]; then
	set -- $TTYALL
	OTTER=./bin/otter
	#OTTER=./DerivedData/otter/Build/Products/Debug/otter
	echo $OTTER $1 115200 "python parsers/ubx_gnss_nav.py"
	$OTTER $1 115200 "python parsers/ubx_gnss_nav.py"
else
	echo "Suitable tty.usbmodem device not found." 1>&2
	exit 1
fi


