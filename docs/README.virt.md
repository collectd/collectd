Inside the virt plugin
======================

Originally written: 20161111

Last updated: 20161115

This is a rewrite of the collectd virt plugin as found in collectd <= 5.6.0,
enhanced to use the libvirt [bulk stats API](https://libvirt.org/html/libvirt-libvirt-domain.html#virDomainListGetStats)
and to add support to deal with with unresponsive domains.

In the reminder of this document, we consider

* libvirt <= 2.0.0
* QEMU <= 2.6.0



High level overview: libvirt client, libvirt daemon, qemu
---------------------------------------------------------

Before to dig into the details, it is useful to review how
the client application (collectd + virt plugin), the libvirtd daemon and the
QEMU processes interact with each other.

The libvirt daemon talks to QEMU using the JSON QMP protcol over one unix domain socket.
The details of the protocol are not important now, but the key part is that the protocol
is a simple request/response, meaning that libvirtd must serialize all the interactions
with the QEMU monitor, and must protects its endpoint with a lock.
This means that if for any reason one QMP request could not be completed, any other caller
trying to access the QEMU monitor will block until the blocked caller returns.

To retrieve some key informations, most notably about the block device state or the balloon
device state, the libvirtd daemon *must* use the QMP protocol.

The QEMU core, including the handling of the QMP protocol, is single-threaded.
All the above combined make it possible for a client to block forever waiting for one QMP
request, if QEMU itself is blocked. The most likely cause of block is I/O, and this is especially
true considering how QEMU is used in a datacenter.

Dealing with datacenters: libvirt, qemu, shared storage
-------------------------------------------------------

When used in a datacenter, QEMU is most often configured to use shared storage. This is
the default configuration of datacenter management solutions like [oVirt](http://www.ovirt.org).
The actual shared storage could be implemented on top of NFS for small installations, or most likely
ISCSI or Fiber Channel. The key takeaway is that the storage is accessed over the network,
not using e.g. the SATA or PCI bus of any given host, so any network issue could cause
one or more storage operations to delay, or to be lost entirely.

In that case, the userspace process that requested the operation can end up in the D state,
and become unresponsive, and unkillable.

Dealing with unresponsive domains
---------------------------------

All the above considered, one robust management or monitoring application must deal with the fact that
the libvirt API can block for a long time, or forever. This is not an issue or a bug of one specific
API, but it is rather a byproduct of how libvirt and QEMU interact.

Whenever we query more than one VM, we should take care to avoid that one blocked VM prevent other,
well behaving VMs to be queried. We don't want one rogue VM to disrupt well-behaving VMs.
Unfortunately, any way we enumerate VMs, either implicitely, using the libvirt bulk stats API,
or explicitely, listing all libvirt domains and query each one in turn, we may unpredictably encounter
one unresponsive VM.

There are many possible approaches to deal with this issue. The virt plugin supports
a simple but effective approach partitioning the domains, as follows.

1. The virt plugin always register one or more `read` callbacks. The `zero` read callback is guaranteed to
   be always present, so it performs special duties (more details later)
   Each callback will be named 'virt-$N', where `N` ranges from 0 (zero) to M-1, where M is the number of instances configured.
   `M` equals to `5` by default, because this is the same default number of threads in the libvirt worker pool.
2. Each of the read callbacks queries libvirt for the list of all the active domains, and retrieves the libvirt domain metadata.
   Both of those operations are safe wrt domain blocked in I/O (they affect only the libvirtd daemon).
3. Each of the read callbacks extracts the `tag` from the domain metadata using a well-known format (see below).
   Each of the read callbacks discards any domain which has no tag, or whose tag doesn't match with the read callback tag.
3.a. The read callback tag equals to the read callback name, thus `virt-$N`. Remember that `virt-0` is guaranteed to be
     always present.
3.b. Since the `virt-0` reader is always present, it will take care of domains with no tag, or with unrecognized tag.
     One unrecognized tag is any tag which has not the scheme `virt-$N`.
4. Each read callback only samples the subset of domains with matching tag. The `virt-0` reader will possibly do more,
   but worst case the load will be unbalanced, no domain will be left unsampled.

To make this approach work, some entity must attach the tags to the libvirt domains, in such a way that all
the domains which run on a given host and insist on the same network-based storage share the same tag.
This minimizes the disruption, because when using the shared storage, if one domain becomes unresponsive because
of unavailable storage, the most likely thing to happen is that others domain using the same storage will soon become
unavailable; should the box run other libvirt domains using other network-based storage, they could be monitored
safely.

In case of [oVirt](http://www.ovirt.org), the aforementioned tagging is performed by the host agent.

Please note that this approach is ineffective if the host completely lose network access to the storage network.
In this case, however, no recovery is possible and no damage limitation is possible.

Lastly, please note that if the virt plugin is configured with instances=1, it behaves exactly like the old virt plugin.

Examples
--------

### Example one: 10 libvirt domains named "domain-A" ... "domain-J", virt plugin with instances=5, using 5 different tags


    libvirt domain name -    tag    - read instance - reason
    domain-A                virt-0         0          tag match
    domain-B                virt-1         1          tag match
    domain-C                virt-2         2          tag match
    domain-D                virt-3         3          tag match
    domain-E                virt-4         4          tag match
    domain-F                virt-0         0          tag match
    domain-G                virt-1         1          tag match
    domain-H                virt-2         2          tag match
    domain-I                virt-3         3          tag match
    domain-J                virt-4         4          tag match


  Because the domain where properly tagged, all the read instances have even load. Please note that the the virt plugin
  knows nothing, and should know nothing, about *how* the libvirt domain are tagged. This is entirely up to the
  management system.


Example two: 10 libvirt domains named "domain-A" ... "domain-J", virt plugin with instances=3, using 5 different tags


    libvirt domain name -    tag    - read instance - reason
    domain-A                virt-0         0          tag match
    domain-B                virt-1         1          tag match
    domain-C                virt-2         2          tag match
    domain-D                virt-3         0          adopted by instance #0
    domain-E                virt-4         0          adopted by instance #0
    domain-F                virt-0         0          rag match
    domain-G                virt-1         1          tag match
    domain-H                virt-2         2          tag match
    domain-I                virt-3         0          adopted by instance #0
    domain-J                virt-4         0          adopted by instance #0


  In this case we have uneven load, but no domain is ignored.


### Example three: 10 libvirt domains named "domain-A" ... "domain-J", virt plugin with instances=5, using 3 different tags


    libvirt domain name -    tag    - read instance - reason
    domain-A                virt-0         0          tag match
    domain-B                virt-1         1          tag match
    domain-C                virt-2         2          tag match
    domain-D                virt-0         0          tag match
    domain-E                virt-1         1          tag match
    domain-F                virt-2         2          tag match
    domain-G                virt-0         0          tag match
    domain-H                virt-1         1          tag match
    domain-I                virt-2         2          tag match
    domain-J                virt-0         0          tag match


  Once again we have uneven load and two idle read instances, but besides that no domain is left unmonitored


### Example four: 10 libvirt domains named "domain-A" ... "domain-J", virt plugin with instances=5, partial tagging


    libvirt domain name -    tag    - read instance - reason
    domain-A                virt-0         0          tag match
    domain-B                virt-1         1          tag match
    domain-C                virt-2         2          tag match
    domain-D                virt-0         0          tag match
    domain-E                               0          adopted by instance #0
    domain-F                               0          adopted by instance #0
    domain-G                               0          adopted by instance #0
    domain-H                               0          adopted by instance #0
    domain-I                               0          adopted by instance #0
    domain-J                               0          adopted by instance #0


The lack of tags causes uneven load, but no domain are unmonitored.


Note about the libvirt bulk stats API
--------------------------------------

The rewritten virt plugin makes use of the libvirt [bulk stats API](https://libvirt.org/html/libvirt-libvirt-domain.html#virDomainListGetStats).
This API provides all the data the more specific calls provide, but in one go and in one
unified output format.
It is worth to highlight that the above issues related to the unresponsive domains are *not* a byproduct
of the use of the bulk stats API. The very same set of issue arises if we use finer grained libvirt APIs.
The issue could arise anytime we need to access the QEMU monitor when QEMU is stuck in one I/O operation,
not how we access it.


Possible extensions - custom tag format
---------------------------------------

The aformentioned approach relies on fixed tag format, `virt-$N`. The algorithm works fine with any tag, which
is just one string, compared for equality. However, using custom strings for tags creates the need for a mapping
between tags and the read instances.
This mapping needs to be updated as long as domain are created or destroyed, and the virt plugin needs to be
notified of the changes.

This adds a significant amount of complexity, with little gain with respect to the fixed schema adopted initially.
For this reason, the introdution of dynamic, custom mapping was not implemented.

Libvirt tag metadata format
----------------------------

This is the snipped to be added to libvirt domains:

    <ovirtmap:tag xmlns:ovirtmap="http://ovirt.org/ovirtmap/tag/1.0">
      $TAG
    </ovirtmap:tag>

it must be included in the <metadata> section.

Check the `src/virt_test.c` file for really minimal example of libvirt domains.
