#!/bin/sh

OS_TYPE=$(uname -s)
PREFIX=${PREFIX:=/usr/local}

rm -f "${PREFIX}/share/man/man8/diskroaster.8.gz"

if [ "$OS_TYPE" = "linux-gnu" ]
then
    mandb --quiet
fi
