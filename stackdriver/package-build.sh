#!/bin/bash -xe
VERSION=5.5.0

if [ "x$BRANCH" == "x" ]
then
    BRANCH=stackdriver-agent-$VERSION
fi

if [ "x$PKGFORMAT" == "xdeb" ]
then
    # debian denotes 64 arches with amd64
    [ "x$ARCH" == "xx86_64" ] && ARCH="amd64" || true
    git clone git@github.com:Stackdriver/agent-deb.git --branch $BRANCH
    pushd agent-deb
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
    git clone git@github.com:Stackdriver/agent-rpm.git --branch $BRANCH
    pushd agent-rpm
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

