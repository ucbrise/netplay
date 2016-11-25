#!/bin/sh

# Install required packages
apt-get -y install zip unzip
apt-get -y install autoconf libtool make cmake

num_cores=`cat /proc/cpuinfo | grep processor | wc -l`

# Download DPDK
cd /home/vagrant
echo "Downloading DPDK..."
wget --quiet http://dpdk.org/browse/dpdk/snapshot/dpdk-16.07.zip
unzip -qq dpdk-16.07.zip
rm dpdk-16.07.zip
export DPDK_DIR=/home/vagrant/dpdk-16.07
cd $DPDK_DIR

export DPDK_TARGET=x86_64-native-linuxapp-gcc
export DPDK_BUILD=$DPDK_DIR/$DPDK_TARGET
make -j${num_cores} install T=$DPDK_TARGET DESTDIR=/usr/local

# Install OVS
cd /home/vagrant
git clone https://github.com/openvswitch/ovs.git
cd ovs/
$( utilities/ovs-dev.py env )
./utilities/ovs-dev.py --O3 --Ofast --with-dpdk=$DPDK_BUILD conf make

# Install Thrift
cd /home/vagrant
git clone https://git-wip-us.apache.org/repos/asf/thrift.git
cd thrift
./bootstrap.sh
./configure --with-lua=no
make -j${num_cores}
make install
cd ..

# Switch ownership of $HOME back to vagrant
chown -R vagrant:vagrant /home/vagrant
