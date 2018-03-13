#!/bin/bash
cd -- "$(dirname "$0")"

TTYALL="$(ls /dev/tty.usbmodem*)"

if [ "$?" = "0" ]; then
	set -- $TTYALL
	OTTER=./bin/otter
	echo $OTTER $1 115200
	$OTTER $1 115200
else
	echo "Suitable tty.usbmodem device not found." 1>&2
	exit 1
fi


