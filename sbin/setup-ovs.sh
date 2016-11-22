#!/bin/bash
set -o errexit
# args
# 1: Master core (4)
# 2: Interface 1 (0000:00:08.0)
# 3: Interface 2 (0000:00:09.0)

BASE_DIR="$( cd "$( dirname "${BASH_SOURCE[0]}" )" && pwd)"
OVS_HOME="$BASE_DIR/../../ovs"
DPDK_LIB="$BASE_DIR/../../dpdk-16.07/x86_64-native-linuxapp-gcc/lib"
export LD_LIBRARY_PATH="${DPDK_LIB}:${LD_LIBRARY_PATH}"
echo $OVS_HOME

INP_LCORE=${1-"1"}
MASTER_LCORE=$((INP_LCORE - 1))
MASTER_COREMASK=$( printf "0x%x" $(( 2**${MASTER_LCORE} )) )
CORE0=$((MASTER_LCORE + 1))
#CORE1=$((MASTER_LCORE + 2))
PMD_MASK=$(printf "0x%x" $((2**(CORE0))))

IFACE1=${2-"0000:00:08.0"}
IFACE2=${3-"0000:00:09.0"}

pushd $OVS_HOME
$( $OVS_HOME/utilities/ovs-dev.py env )
$OVS_HOME/utilities/ovs-dev.py \
  reset run --dpdk -c ${MASTER_COREMASK} -n 4 -r 1 --socket-mem 1024,0 \
	--file-prefix "ovs" -w ${IFACE1},${IFACE2}

ovs-vsctl set Open . other_config:n-dpdk-rxqs=1
ovs-vsctl add-br b -- set bridge b datapath_type=netdev
ovs-vsctl set Open . other_config:pmd-cpu-mask="$PMD_MASK"
ovs-vsctl set Open . other_config:n-handler-threads=1
ovs-vsctl set Open . other_config:n-revalidator-threads=1
ovs-vsctl set Open . other_config:max-idle=10000

iface="dpdk0"
echo "Setting up physical interface ${iface}"
ovs-vsctl add-port b ${iface} -- set Interface ${iface} type=dpdk

# iface="dpdk1"
# echo "Setting up physical interface ${iface}"
# ovs-vsctl add-port b ${iface} -- set Interface ${iface} type=dpdk

iface="dpdkr0"
echo "Setting up DPDK ring interface ${iface}"
ovs-vsctl add-port b ${iface} -- set Interface ${iface} type=dpdkr

ovs-ofctl del-flows b

# Reroute traffic from dpdk0 -> dpdkr0
src_port=1
dst_port=2
echo "ovs-ofctl add-flow b in_port=${src_port},actions=output:${dst_port}"
ovs-ofctl add-flow b in_port=${src_port},actions=output:${dst_port}

# Mirror dpdk1 -> dpdkr0
