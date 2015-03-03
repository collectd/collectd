#!/bin/bash -xe
VERSION=5.5.0

if [ "x$PKGFORMAT" == "xdeb" ]
then
    # debian denotes 64 arches with amd64
    [ "x$ARCH" == "xx86_64" ] && ARCH="amd64" || true
    git clone git@github.com:Stackdriver/agent-deb.git
    pushd agent-deb
    git fetch origin stackdriver-agent-$VERSION
    git checkout stackdriver-agent-$VERSION
    make clean
    make DISTRO="$DISTRO" ARCH="$ARCH" VERSION="$VERSION" BUILD_NUM="$BUILD_NUM" build
    if [ $? -ne 0 ]
    then
        exit $?
    fi
	popd
	[ -d result ] && rm -rf result || true
	cp -r agent-deb/result .
elif [ "x$PKGFORMAT" == "xrpm" ]
then
    git clone git@github.com:Stackdriver/agent-rpm.git
    pushd agent-rpm
    git fetch origin stackdriver-agent-$VERSION
    git checkout stackdriver-agent-$VERSION
    make clean
    make DISTRO="$DISTRO" ARCH="$ARCH" VERSION="$VERSION" BUILD_NUM="$BUILD_NUM" build
    if [ $? -ne 0 ]
    then
        exit $?
    fi
    popd
    [ -d result ] && rm -rf result || true
	cp -r agent-rpm/result .
else
    echo "I don't know how to handle label '$PKGFORMAT'. Aborting build"
    exit 1
fi

