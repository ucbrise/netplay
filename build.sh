#!/bin/bash
set -e

mkdir -p build
cd build
if [[ "$OSTYPE" == "darwin"* ]]; then
  echo "FATAL: Operating system not supported."
  exit -1
else
  cmake .. $@
fi

START=$(date +%s)
make -j
make test ARGS="-VV"
END=$(date +%s)
echo "Total Build time (real) = $(( $END - $START )) seconds"
