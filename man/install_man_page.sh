#!/bin/sh

OS_TYPE=$(uname -s)
install -d /usr/local/share/man/man8

if [ "$OS_TYPE" = "FreeBSD" ]
then
	cp ./man/diskroaster.8.freebsd /usr/local/share/man/man8/diskroaster.8
	gzip -f /usr/local/share/man/man8/diskroaster.8
elif [ "$OS_TYPE" = "Linux" ]
then
	cp ./man/diskroaster.8.linux /usr/local/share/man/man8/diskroaster.8
	gzip -f /usr/local/share/man/man8/diskroaster.8
	mandb --quiet
fi

