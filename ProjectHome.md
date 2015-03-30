**IMPORTANT** for the latest and greatest version of Click, also including
netmap support, please refer to the master Click repository at
https://github.com/kohler/click.git , which will periodically merge the stable
versions of netmap support.


---


This repository is working space for experimental versions of netmap
support for the Click modular router.
It is loosely kept in sync with the code at https://code.google.com/p/netmap
and with the master Click repository https://github.com/kohler/click.git .

**PLEASE USE THE MAIN CLICK REPOSITORY FOR RUNNING CLICK**, the code you
find here is likely to be out of date.


This version matches the current version of [netmap/VALE](http://info.iet.unipi.it/~luigi/netmap),
supporting all features (including netmap pipes),
and uses greatly simplified code to access netmap ports. Zerocopy is not yet supported and will be added at a later time.
Performance is already excellent anyways (8-12Mpps with a single thread).

See http://info.iet.unipi.it/~luigi/netmap for more details on netmap.

Other related repositories of interest (in all cases we track the original repositories and will try to upstream our changes):

  * https://code.google.com/p/netmap the latest version of netmap source code (kernel module and examples) for FreeBSD and Linux. Note, FreeBSD distribution include netmap natively.
  * https://code.google.com/p/netmap-libpcap a netmap-enabled version of libpcap from https://github.com/the-tcpdump-group/libpcap.git . With this, basically any pcap client can read/write traffic at 10+ Mpps, with zerocopy reads and (soon) support for zerocopy writes
  * https://code.google.com/p/netmap-ipfw a netmap-enabled, userspace version of the ipfw firewall and dummynet network emulator. This version reaches 7-10 Mpps for filtering and over 2.5 Mpps for emulation.
  * https://code.google.com/p/netmap-click/ this repository (for cut&paste convenience).