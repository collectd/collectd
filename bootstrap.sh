#!/bin/sh
set -eux

readonly DO_CLEAN=true # Clean up previous build results (true/false)

readonly MYDIR="$(dirname "$(readlink -f "$0")")" # Directory this script is in
readonly PREFIX="$MYDIR/install"                  # Where to install collectd

# Install dependencies
sudo apt install liboping-dev libcurl4-openssl-dev libyajl-dev socat

# Go to collectd root
cd "$MYDIR"

# Clean up previous build results
if $DO_CLEAN; then
    ./clean.sh
    rm -rf "$PREFIX"
fi

# Rebuild
./build.sh
./configure --prefix="$PREFIX" \
    --disable-all-plugins \
    --enable-cpu \
    --enable-memory \
    --enable-ping \
    --enable-write-http \
    --enable-write-log
make -j"$(grep -c '^processor' /proc/cpuinfo)"

# Install to collectd/install/*
make install

# Make the ping plugin work without being root
sudo setcap cap_net_raw+ep "$PREFIX"/sbin/collectd

# Copy configuration
readonly SRC_CONF="$MYDIR/collectd.conf"      # source config to copy
readonly TGT_CONF="$PREFIX/etc/collectd.conf" # target where collectd expects it
if [ ! -e "$TGT_CONF".orig ]; then
    # Make a backup for reference
    cp -v "$TGT_CONF" "$TGT_CONF".orig || true
fi
cp -v "$SRC_CONF" "$TGT_CONF"

# Start collectd
"$PREFIX"/sbin/collectd -f
