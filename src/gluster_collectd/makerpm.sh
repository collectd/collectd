#!/bin/bash

# Simple script to create RPMs for G4S

## RPM NAME
RPMNAME=gluster_collectd

cleanup()
{
    rm -rf ${RPMBUILDDIR} > /dev/null 2>&1
    rm -f ${PKGCONFIG} > /dev/null 2>&1
}

fail()
{
    cleanup
    echo $1
    exit $2
}

create_dir()
{
    if [ ! -d "$1" ] ; then
        mkdir -p "$1"
        if [ $? -ne 0 ] ; then
            fail "Unable to create dir $1" $?
        fi
    fi
}

gittotar()
{
    # Only archives committed changes
    gitarchive_dir="${RPMBUILDDIR}/gitarchive"
    specfile="${gitarchive_dir}/${SRCTAR_DIR}/${RPMNAME}.spec"
    create_dir "${gitarchive_dir}"

    # Export the current commited git changes to a directory
    git archive --format=tar --prefix=${SRCTAR_DIR}/ HEAD | (cd ${gitarchive_dir} && tar xf -)
    # Create a new spec file with the current package version information
    sed -e "s#__PKG_RELEASE__#${PKG_RELEASE}#" \
        -e "s#__PKG_NAME__#${RPMNAME}#" \
        -e "s#__PKG_VERSION__#${PKG_VERSION}#" \
        ${specfile} > ${specfile}.new
    mv ${specfile}.new ${specfile}

    # Now create a tar file
    ( cd ${gitarchive_dir} && tar cf - ${SRCTAR_DIR} | gzip -c > ${SRCTAR} )
    if [ $? -ne 0 -o \! -s ${SRCTAR} ] ; then
        fail "Unable to create git archive" $?
    fi
}

prep()
{
    rm -rf ${RPMBUILDDIR} > /dev/null 2>&1
    create_dir ${RPMBUILDDIR}

    # Create a tar file out of the current committed changes
    gittotar

}

create_rpm()
{
    # Create the rpm
    # _topdir Notifies rpmbuild the location of the root directory
    #         containing the RPM information
    # _release Allows Jenkins to setup the version using the
    #          build number
    rpmbuild --define "_topdir ${RPMBUILDDIR}" \
        -ta ${SRCTAR}
    if [ $? -ne 0 ] ; then
        fail "Unable to create rpm" $?
    fi

    # Move the rpms to the root directory
    mv ${RPMBUILDDIR_RPMS}/noarch/*rpm ${BUILDDIR}
    mv ${RPMBUILDDIR_SRPMS}/*rpm ${BUILDDIR}
    if [ $? -ne 0 ] ; then
        fail "Unable to move rpm to ${BUILDDIR}" $?
    fi

    echo "RPMS are now available in ${BUILDDIR}"
}

create_src()
{
    python setup.py sdist --format=gztar --dist-dir=${BUILDDIR}
    if [ $? -ne 0 ] ; then
        fail "Unable to create source archive"
    fi
}

################## MAIN #####################

# Create a config file with the package information
PKGCONFIG=${PWD}/pkgconfig.in
env python pkgconfig.py
if [ ! -f "${PKGCONFIG}" ] ; then
    fail "Unable to create package information file ${PKGCONFIG}" 1
fi

# Get package version information
. ${PKGCONFIG}
if [ -z "${NAME}" ] ; then
    fail "Unable to read the package name from the file created by pkgconfig.py" 1
fi
if [ -z "${VERSION}" ] ; then
    fail "Unable to read the package version from the file created by pkgconfig.py" 1
fi
if [ -z "${RELEASE}" ] ; then
    fail "Unable to read the package version from the file created by pkgconfig.py" 1
fi

PKG_NAME=$NAME
PKG_VERSION=$VERSION

#
# This can be set by JENKINS builds
# If the environment variable PKG_RELEASE
# has not been set, then we set it locally to
# a default value
#
if [ -z "$PKG_RELEASE" ] ; then
    PKG_RELEASE="${RELEASE}"
else
    PKG_RELEASE="${RELEASE}.${PKG_RELEASE}"
fi

BUILDDIR=$PWD/build
RPMBUILDDIR=${BUILDDIR}/rpmbuild
RPMBUILDDIR_RPMS=${RPMBUILDDIR}/RPMS
RPMBUILDDIR_SRPMS=${RPMBUILDDIR}/SRPMS
SRCNAME=${RPMNAME}-${PKG_VERSION}-${PKG_RELEASE}
SRCTAR_DIR=${PKG_NAME}-${PKG_VERSION}
SRCTAR=${RPMBUILDDIR}/${SRCNAME}.tar.gz

prep
create_src
create_rpm
cleanup
