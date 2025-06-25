#!/bin/sh

if [ $OSTYPE = "FreeBSD" ]
then
	rm -f /usr/local/man/man1/diskroaster.1.gz
elif [ $OSTYPE = "linux-gnu" ]
then
	rm -f /usr/local/share/man/man1/diskroaster.1.gz
	mandb --quiet
fi

