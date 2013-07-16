# simple makefile to handle grabbing deb and rpm repos and setting off their builds

VERSION=5.3.0
SHELL=/bin/bash

source:
	ifeq ($(PKGFORMAT),deb)
		[ -d agent-deb.git ] || git clone https://github.com/Stackdriver/agent-deb.git
		pushd agent-deb ; \
		git pull ; \
		popd
	else ifeq ($(PKGFORMAT),rpm)
		[ -d agent-rpm.git ] || git clone https://github.com/Stackdriver/agent-rpm.git
		pushd agent-rpm ; \
		git pull ; \
		popd
	else
		echo "I don't know how to handle label '$(PKGFORMAT)'. Aborting build"
		exit 1
	endif

build: source
	ifeq ($(PKGFORMAT),deb)
		pushd agent-deb ; \
		make DISTRO="$(DISTRO)" ARCH="$(ARCH)" VERSION="$(VERSION)" BUILD_NUM="$(BUILD_NUM)" build ; \
		popd
	else ifeq ($(PKGFORMAT),rpm)
		pushd agent-rpm ; \
		make DISTRO="$(DISTRO)" ARCH="$(ARCH)" VERSION="$(VERSION)" BUILD_NUM="$(BUILD_NUM)" build ; \
		popd
	else
	    echo "I don't know how to handle label '$(PKGFORMAT)'. Aborting build"
		exit 1
	endif

clean:
	rm -rf *.dsc *.deb *.tar.gz *.tar.bz2 agent-deb agent-rpm result apt *.rpm
