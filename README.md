# NetPlay

NetPlay is a system for real-time packet monitoring and diagnosis.

## Supported Platforms

NetPlay has currently only been tested on Ubuntu 14.04 and later, although it
should be possible to tweak the build scripts to make it work on other Linux
distributions.

We plan on providing scripts for other operating systems quite soon.

## Dependencies

NetPlay has the following major dependencies:

* DPDK
* A virtual switch implementation (OVS by default, BESS is also supported.<sup>\*</sup>)
  
<sup>\*</sup>Contact us if you want to use NetPlay with other virtual switch 
implementations.

Install all (major and minor) dependencies on Ubuntu 14.04 or later as follows:

```bash
$NETPLAY_HOME/3rdparty/setup.sh
```

Where `$NETPLAY_HOME` is the NetPlay directory.

## Building

NetPlay uses CMake as its build platform. Ensure you have CMake 2.8 or later.

In order to build NetPlay, simply use the provided build script as follows:

```bash
$NETPLAY_HOME/build.sh
```

This will also run all tests by default.

## Using NetPlay

TODO: Add description.

## Contact Us

Anurag Khandelwal (anuragk@berkeley.edu)
