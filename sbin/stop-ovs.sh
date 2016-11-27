#!/bin/bash
set -o errexit
# args
# 1: Master core (4)

BASE_DIR="$( cd "$( dirname "${BASH_SOURCE[0]}" )" && pwd)"
OVS_HOME="$BASE_DIR/../3rdparty/ovs"
DPDK_LIB="$BASE_DIR/../3rdparty/dpdk-16.07/x86_64-native-linuxapp-gcc/lib"
export LD_LIBRARY_PATH="${DPDK_LIB}:${LD_LIBRARY_PATH}"
echo $OVS_HOME

pushd $OVS_HOME
$( $OVS_HOME/utilities/ovs-dev.py env )
$OVS_HOME/utilities/ovs-dev.py reset
