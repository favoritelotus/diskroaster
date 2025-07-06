#!/bin/sh

rm -f /usr/local/share/man/man8/diskroaster.8.gz

if [ $OSTYPE = "linux-gnu" ]
then
    mandb --quiet
fi
