# The dpdkstat plugin

**Data Plane Development Kit** (DPDK) is a set of drivers and libraries for fast
packet processing.

## Summary

The *dpdkstat plugin* has the following requirements:

 * DPDK 16.04 or later
 * GCC 4.9 or later

You can also build with GCC 4.8 (e.g. Ubuntu 14.04) if you specify the SSSE3
instruction set manually:

    make -j CFLAGS+='-mssse3'

## Building DPDK

 *  Setup the build environment:

    Ensure that you have GCC 4.9 or later. Ubuntu 14.04, for example, has GCC
    4.8 by default and requires an upgrade:

        add-apt-repository ppa:ubuntu-toolchain-r/test
        apt-get update
        apt-get install gcc-4.9

    If you know that the platform that you wish to run collectd on supports the
    SSSE3 instruction set, GCC 4.8 also works if you enable SSSE3 manually:

        make -j CFLAGS+='-mssse3'

 *  Clone DPDK:

        git clone git://dpdk.org/dpdk

 *  Checkout the [DPDK system
    requirements](http://dpdk.org/doc/guides/linux_gsg/sys_reqs.html) and make
    sure you have the required tools and hugepage setup as specified there.

    **Note:** It's recommended to use the 1GB hugepage setup for best
    performance, please follow the instruction for "Reserving Hugepages for DPDK
    Use" in the link above.

    However if you plan on configuring 2MB hugepages on the fly please ensure to
    add appropriate commands to reserve hugepages in a system startup script if
    collectd is booted at system startup time. These commands include:

        mkdir -p /mnt/huge
        mount -t hugetlbfs nodev /mnt/huge
        echo 64 > /sys/devices/system/node/node0/hugepages/hugepages-2048kB/nr_hugepages

 *  To configure the DPDK build for the combined shared library modify
    `config/common_base` in your DPDK as follows

        #
        # Compile to share library
        #
        -CONFIG_RTE_BUILD_SHARED_LIB=n
        +CONFIG_RTE_BUILD_SHARED_LIB=y

 *  Prepare the configuration for the appropriate target as specified at:
    http://dpdk.org/doc/guides/linux_gsg/build_dpdk.html.

    For example:

        make config T=x86_64-native-linuxapp-gcc

 *  Build the target:

        make

 *  Install DPDK to `/usr`

        sudo make install prefix=/usr

    **Note 1:** You must run make install as the configuration of collectd with
    DPDK expects DPDK to be installed somewhere.

    **Note 2:** If you don't specify a prefix then DPDK will be installed in
    `/usr/local/`.

    **Note 3:** If you are not root then use sudo to make install DPDK to the
    appropriate location.

 *  Check that the DPDK library has been installed in `/usr/lib` or `/lib`:

        ls /usr/lib | grep dpdk

 *  Bind the interfaces to use with dpdkstat to DPDK:

    DPDK devices can be setup with either the VFIO (for DPDK 1.7+) or UIO
    modules.

    **Note:** UIO requires inserting an out of tree driver `igb_uio.ko` that is
    available in DPDK.

    **UIO Setup:**

     *  Insert `uio.ko`:

            sudo modprobe uio

     *  Insert `igb_uio.ko`:

            sudo insmod $DPDK_BUILD/kmod/igb_uio.ko

     *  Bind network device to `igb_uio`:

            sudo $DPDK_DIR/tools/dpdk_nic_bind.py --bind=igb_uio eth1

    **VFIO Setup:**

     *  VFIO needs to be supported in the kernel and the BIOS. More information
        can be found at: http://dpdk.org/doc/guides/linux_gsg/build_dpdk.html.
     *  Insert the `vfio-pci.ko` module:

            modprobe vfio-pci

     *  Set the correct permissions for the VFIO device:

            sudo /usr/bin/chmod a+x /dev/vfio
            sudo /usr/bin/chmod 0666 /dev/vfio/*

     *  Bind the network device to `vfio-pci`:

            sudo $DPDK_DIR/tools/dpdk_nic_bind.py --bind=vfio-pci eth1

        **Note:** Please ensure to add appropriate commands to bind the network
        interfaces to DPDK in a system startup script if collectd is booted at
        system startup time.

     *  Run `ldconfig` to update the shared library cache.

### Static library

To build static DPDK library for use with collectd:

 *  To configure DPDK to build the combined static library `libdpdk.a` ensure
    that `CONFIG_RTE_BUILD_SHARED_LIB` is set to “n” in `config/common_base` in
    your DPDK as follows:

        #
        # Compile to share library
        #
        CONFIG_RTE_BUILD_SHARED_LIB=n

 *  Prepare the configuration for the appropriate target as specified at:
    http://dpdk.org/doc/guides/linux_gsg/build_dpdk.html.

    For example:

        make config T=x86_64-native-linuxapp-gcc

 *  Build the target using `-fPIC`:

        make EXTRA_CFLAGS=-fPIC -j

 *  Install DPDK to `/usr`:

        sudo make install prefix=/usr

## Build collectd with DPDK

**Note:** DPDK 16.04 is the minimum version and currently supported version of
DPDK required for the dpdkstat plugin. This is to allow the plugin to take
advantage of functions added to detect if the DPDK primary process is alive.


**Note:** The *Address-Space Layout Randomization* (ASLR) security feature in
Linux should be disabled, in order for the same hugepage memory mappings to be
present in all DPDK multi-process applications. Note that this has security
implications.

 *  To disable ASLR:

        echo 0 > /proc/sys/kernel/randomize_va_space

 *  To fully enable ASLR:

        echo 2 > /proc/sys/kernel/randomize_va_space

See also: http://dpdk.org/doc/guides/prog_guide/multi_proc_support.html

 *  Generate the build script as specified below. (i.e. run `build.sh`).
 *  Configure collectd with the DPDK shared library:

        ./configure --with-libdpdk=/usr

### Build with the static DPDK library

To configure collectd with the DPDK static library:

 *  Run *configure* with the following CFLAGS:

        ./configure --with-libdpdk=/usr CFLAGS=" -lpthread -Wl,--whole-archive -Wl,-ldpdk -Wl,-lm -Wl,-lrt -Wl,-lpcap -Wl,-ldl -Wl,--no-whole-archive"

 *  Make sure that dpdk and dpdkstat are enabled in the *configure* output.

    Expected output:

        Libraries:
        ...
        libdpdk  . . . . . . . . yes
        
        Modules:
        ...
        dpdkstat . . . . . . .yes

 *  Build collectd:

        make -j && make -j install.

    **Note:** As mentioned above, if you are building on Ubuntu 14.04 with
    GCC <= 4.8.X, you need to use:

        make -j CFLAGS+='-mssse3' && make -j install

## Caveats

 *  The same PCI device configuration should be passed to the primary process as
    the secondary process uses the same port indexes as the primary.
 *  A blacklist / whitelist of NICs isn't supported yet.

## License

The *dpdkstat plugin* is copyright (c) 2016 *Intel Corporation* and licensed
under the *MIT license*. Full licensing terms can be found in the file
`COPYING`.
