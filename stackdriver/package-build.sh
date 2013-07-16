#!/bin/bash
VERSION=5.3.0

if [ "x$PKGFORMAT" == "xdeb" ]
then
    [ -d agent-deb ] || git clone git@github.com:Stackdriver/agent-deb.git
    pushd agent-deb
    git pull
    make DISTRO="$DISTRO" ARCH="$ARCH" VERSION="$VERSION" BUILD_NUM="$BUILD_NUM" build
    if [ $? -ne 0 ]
    then
        exit $?
    fi
	popd
elif [ "x$PKGFORMAT" == "xrpm" ]
then
    [ -d agent-rpm ] || git clone git@github.com:Stackdriver/agent-rpm.git
    pushd agent-rpm
    git pull
    make DISTRO="$DISTRO" ARCH="$ARCH" VERSION="$VERSION" BUILD_NUM="$BUILD_NUM" build
    if [ $? -ne 0 ]
    then
        exit $?
    fi
    popd
else
    echo "I don't know how to handle label '$PKGFORMAT'. Aborting build"
    exit 1
fi

