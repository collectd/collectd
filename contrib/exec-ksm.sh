#!/bin/bash

# collectd - contrib/exec-ksm.sh
# Copyright (C) 2011  Florian Forster
#
# Permission is hereby granted, free of charge, to any person obtaining a
# copy of this software and associated documentation files (the "Software"),
# to deal in the Software without restriction, including without limitation
# the rights to use, copy, modify, merge, publish, distribute, sublicense,
# and/or sell copies of the Software, and to permit persons to whom the
# Software is furnished to do so, subject to the following conditions:
#
# The above copyright notice and this permission notice shall be included in
# all copies or substantial portions of the Software.
#
# THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
# IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
# FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
# AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
# LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
# FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
# DEALINGS IN THE SOFTWARE.
#
# Authors:
#   Florian Forster <octo at collectd.org>

HOSTNAME="${COLLECTD_HOSTNAME:-$(hostname -f)}"
INTERVAL="${COLLECTD_INTERVAL:-60}"

function read_file() {
  local type="$1"
  local type_instance="$2"
  local file_name="/sys/kernel/mm/ksm/$3"
  local ident="$HOSTNAME/exec-ksm/vmpage_number-${type_instance}"

  echo "PUTVAL \"$ident\" interval=$INTERVAL N:$(< $file_name)"
}

if [[ 0 -eq $(< /sys/kernel/mm/ksm/run) ]]; then
  echo "$0: KSM not active." >&2
  exit 1
fi

while sleep "$INTERVAL"
do
  read_file vmpage_number    shared   pages_shared
  read_file vmpage_number    saved    pages_sharing
  read_file vmpage_number    unshared pages_unshared
  read_file vmpage_number    volatile pages_volatile
  read_file total_operations scan     full_scans
done

exit 0
