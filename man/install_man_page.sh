#!/bin/sh

if [ $OSTYPE = "FreeBSD" ]
then
	install -d /usr/local/man/man1
	cp ./man/diskroaster.1.freebsd /usr/local/man/man1/diskroaster.1
	gzip -f /usr/local/man/man1/diskroaster.1
elif [ $OSTYPE = "linux-gnu" ]
then
	install -d /usr/local/share/man/man1
	cp ./man/diskroaster.1.linux /usr/local/share/man/man1/diskroaster.1
	gzip -f /usr/local/share/man/man1/diskroaster.1
	mandb --quiet
fi

