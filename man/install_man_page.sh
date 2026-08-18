#!/bin/sh

OS_TYPE=$(uname -s)
PREFIX=${PREFIX:=/usr/local}

install -d "${PREFIX}/share/man/man8"

if [ "$OS_TYPE" = "FreeBSD" ]
then
	install -m 0644 ./man/diskroaster.8.freebsd "${PREFIX}/share/man/man8/diskroaster.8"
	gzip -f "${PREFIX}/share/man/man8/diskroaster.8"
elif [ "$OS_TYPE" = "Linux" ]
then
	install -m 0644 ./man/diskroaster.8.linux "${PREFIX}/share/man/man8/diskroaster.8"
	gzip -f "${PREFIX}/share/man/man8/diskroaster.8"
	mandb --quiet
fi

