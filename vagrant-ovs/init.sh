#!/bin/sh

# Upgrade packages
sudo apt-get update
sudo apt-get upgrade -y

# Install basic packages
apt-get install -y git build-essential libpcap-dev zlib1g-dev libssl-dev \
  libnuma libnuma-dev
apt-get install -y gdb gdbserver exuberant-ctags cscope vim emacs
apt-get install -y linux-generic linux-headers-generic linux-virtual \
  linux-tools-common linux-tools-generic linux-image-extra-virtual
apt-get install libboost-dev libboost-test-dev libboost-program-options-dev \
  libboost-filesystem-dev libboost-thread-dev libevent-dev automake libtool \
  flex bison pkg-config g++ libssl-dev

cp /home/vagrant/netplay/vagrant-ovs/etc/sysctl.conf /etc/sysctl.conf
cp /home/vagrant/netplay/vagrant-ovs/etc/rc.local /etc/rc.local

sysctl -p
