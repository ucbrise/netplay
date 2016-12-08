#!/bin/sh
set -e

third_party="$( cd "$( dirname "${BASH_SOURCE[0]}" )" && pwd )"

echo "Working directory: $third_party"

# Upgrade packages
sudo apt-get update
sudo apt-get upgrade -y

# Install basic packages
sudo apt-get install -y linux-generic linux-headers-generic linux-virtual \
  linux-tools-common linux-tools-generic linux-image-extra-virtual \
  libevent-dev automake libtool \
  flex bison pkg-config g++ libssl-dev gdb gdbserver exuberant-ctags cscope \
  vim emacs git build-essential libpcap-dev zlib1g-dev libssl-dev libnuma-dev \
  zip unzip autoconf libtool make cmake

num_cores=`cat /proc/cpuinfo | grep processor | wc -l`

# Download DPDK
cd $third_party
export DPDK_DIR=$third_party/dpdk-16.07
echo "DPDK_DIR=$DPDK_DIR"
if [ ! -d "$DPDK_DIR" ]; then
  echo "Downloading DPDK..."
  wget --quiet http://dpdk.org/browse/dpdk/snapshot/dpdk-16.07.zip
  unzip -qq dpdk-16.07.zip
  rm dpdk-16.07.zip
fi
cd $DPDK_DIR
export DPDK_TARGET=x86_64-native-linuxapp-gcc
make config T=$DPDK_TARGET 
export DPDK_BUILD=$DPDK_DIR/build
cd $DPDK_BUILD
sudo make -j${num_cores}

# Build OVS
cd $third_party
export OVS_DIR=$third_party/ovs
echo "OVS_DIR=$OVS_DIR"
if [ ! -d "$OVS_DIR" ]; then
  git clone https://github.com/openvswitch/ovs.git
fi
cd ovs/
$( utilities/ovs-dev.py env )
./utilities/ovs-dev.py --O3 --Ofast --with-dpdk=$DPDK_BUILD conf make
