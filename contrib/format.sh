#!/bin/sh

# This script sends files to a web service using POST requests and reads back
# the correctly formatted source files. This allows to apply clang-format
# without having to install the tool locally.

if test $# -lt 1; then
  echo "Usage $0 <file> [<file> ...]"
  exit 1
fi

for i in "$@"; do
  d="`dirname "${i}"`"
  o="`TMPDIR="${d}" mktemp format.XXXXXX`"

  curl --silent --data-binary "@-" https://format.collectd.org/ <"${i}" >"${o}"
  if test $? -eq 0; then
    cat "${o}" >"${i}"
  fi
  rm -f "${o}"
done
