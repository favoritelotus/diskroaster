#!/bin/sh

OS_TYPE=$(uname -s)

rm -f /usr/local/share/man/man8/diskroaster.8.gz

if [ "$OS_TYPE" = "linux-gnu" ]
then
    mandb --quiet
fi
