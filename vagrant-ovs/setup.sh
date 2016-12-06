#!/bin/sh

cd /home/vagrant/netplay/3rdparty
./setup.sh
cd ..
./build.sh

# Switch ownership of $HOME back to vagrant
chown -R vagrant:vagrant /home/vagrant

