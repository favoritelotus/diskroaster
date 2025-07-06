#!/bin/sh

install -d /usr/local/share/man/man8

if [ $OSTYPE = "FreeBSD" ]
then
	cp ./man/diskroaster.8.freebsd /usr/local/share/man/man8/diskroaster.8
	gzip -f /usr/local/share/man/man8/diskroaster.8
elif [ $OSTYPE = "linux-gnu" ]
then
	cp ./man/diskroaster.8.linux /usr/local/share/man/man8/diskroaster.8
	gzip -f /usr/local/share/man/man8/diskroaster.8
	mandb --quiet
fi

