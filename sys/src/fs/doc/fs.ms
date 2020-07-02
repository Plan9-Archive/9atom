.EQ
delim $$
.EN
.TL
The Diskless Fileserver
.AU
Erik Quanstrom
quanstro@coraid.com
.AB
The Plan 9 Fileserver is structured as a multilevel cache
for direct-attached WORM storage.  I describe how the
Fileserver is being adapted for modern hardware using
network-attached storage (AoE) over 10Gbps Ethernet.
This structure allows for good performance and high
reliability.  In addition it separates storage maintenance
from Fileserver maintenance and provides automatic offsite
backup without performance penalty.
.AE
.NH
Introduction
.LP
In order to meet our growing performance and reliability
demands, I am in the process of rolling out a diskless
Fileserver.  The system consists of a diskless Intel-based
Fileserver, a local AoE target and an offset AoE target.
A backup Fileserver in “standby” mode is available in case
the main Fileserver should fail.
The AoE targets are stock
.I SR1521
machines with added 10Gbps Ethernet cards.
This configuration is pictured in \*(Fn.
.F1
.PS
scale=10
u=2.5
gap=19
define sr |
[
	box "\f2SR1521\f1"	ht 3*u wid 19
] |
define fs |
[
	box $1		ht u wid 19
] |

A: fs("Fileserver")
B: [sr] at A+(0, -4*u)
C: spline <-> " 10Gbe" from B.nw+(0,-3/2*u)  left .75*u then up 4*u then right .5*u to A.sw+(0,u*.5)
D: fs("backup Fileserver") at A+(gap+19, 0)
E: [sr] at A+(gap+19, -4*u)
F: spline <-> "Wireless" from A.ne+(0,-1/2*u) right gap/2 to E.nw+(0,-3*u/2)
G: spline <-> "10Gbe " from E.ne+(0,-3/2*u)  right .75*u then up 4*u then left .5*u to D.se+(0,u*.5)
.PE
.F2
.F3
.PP
The configuration string[2] for this Fileserver is
.P1
.CW "filsys main ce565.0{e565.1e545.1}" .
.P2
The configuration string for the backup Fileserver is
.P1
.CW "filsys main ce545.0e545.1" .
.P2
The targets
.CW e565.\f2x\fP
are connected to the Fileserver by a point-to-point 10Gbps
Ethernet link.  Except during a dump or in the event of a
failure of
.CW e565.1 ,
all I/O is performed over this link.  The target
.CW e545.0
is in another building, connected by a shared 54Mbps wireless
link.
.PP
The AoE targets are managed independently from the
Fileserver.  Maintenance tasks, like replacing failed drives,
reconfiguring or adding storage, do not require knowledge of
the Fileserver and may be performed without shutting down
the Fileserver.  Conversely, the Fileserver does not require
knowledge of how to perform maintenance on the AoE targets.
.NH
Fileserver Basics
.LP
The Fileserver serves files via the Plan 9 file protocol,
9P2000.  Requests that cannot be directly satisfied by the
in-memory Block Cache are resolved by devices.  The Block
Cache is indexed by device and device address.  The
.CW cw
device serves the WORM filesystem.  It is comprised of three
on-disk devices: cache, read-only and cached
WORM.  These devices are known as
.CW c ,
.CW w ,
and 
.CW cw .
All Blocks have a 
.CW w-address 
and a cache state.
Blocks not in the cache are state
.CW none .
Freshly written blocks are state
.CW write .
Blocks on the 
.CW w
device that are rewritten are state
.CW dirty .
A “dump,” a permanent snapshot of the filesystem, is taken
by converting modified blocks to state
.CW dump .
This process takes just a few seconds.  Other activity on
the Fileserver is halted during the dump.  Copying takes place
in the background and does not impact the performance of the
Fileserver.
Once state
.CW dump
blocks are copied to the WORM, their state is changed to
.CW read 
or
.CW none ,
if it is dropped from the cache.  The copying phase of any
number of dumps may overlap.
.LP
The implemented Fileserver has a Block Cache of
402,197 8192-byte blocks (3137MB), a cache device,
.CW e565.0 ,
of 3,276,800 blocks (25GB) and a WORM device,
.CW "{e565.1e545.1}" ,
of 1.5TB.  The WORM device is the loose mirror
of AoE targets
.CW e565.1
and
.CW e545.1 .
Writes are preformed on the mirrored devices sequentially
but data is read from the first device only.  Thus the wireless
connection which limits dumps to ~1MB/s is not part of
the client's I/O path.
.LP
The WORM filesystem is fully described in [1], [2] and [3].
.NH
“Standby” Mode
.LP
It is not possible use both Fileservers at the same time.  Both
will try to allocate
.CW w-addresses 
without respect to the other.  To solve this problem a
configuration item and command, both named
.CW dumpctl 
were added.
The main Fileserver is configured with
.CW "dumpctl yes"
and the backup Fileserver is configured with
.CW "dumpctl no" .
To prevent writes, attaches may be disallowed.
In the event that the Fileserver fails, the command
.CW "dumpctl yes"
is executed on the backup Fileserver's console.
And if disabled, attaches are allowed.
.LP
While the backup Fileserver is running, it will not
see the new data written by the dump process
on the main Fileserver.  The backup Fileserver must
be halted each day after the dump on the Fileserver
and the command
.CW "recover main"
must be typed at the
.CW config
prompt.  This will cause the cache to be flushed and
the filesystem to be initialized from the new dump.
.NH
Changed Assumptions
.LP
In the fifteen odd years since the Fileserver was developed,
a few of its assumptions have ceased to hold.
The most obvious is the
.CW worm
device is no longer a WORM. Even if we were to use WORM
storage, disk space is inexpensive enough that it would be
practical to keep an entire copy of the WORM on magnetic
storage for performance reasons.  This means that the
cache and the WORM devices have the same performance.
Therefore it no longer makes sense to copy blocks in state
.CW Cread
to the cache device.  Blocks in state
.CW Cread
have been read from the
.CW worm
device but not modified[3].  A new option,
.CW conf.fastworm ,
inhibits copying these blocks to the disk cache.
.PP
A less obvious difference is in the structure of the cache.
The cache device is structured as a hash table.  The hash
function is simply modulo the number of hash lines and
the lines are written sequentially to disk.  If we let
.I n
be the number of rows and
.I l
be the number of columns in our hash, he function
is
.P1
    row = w % rows
    c = column + row*n,
.P2
the blocks will be
linearized onto the disk in the following order
.P1
    $0, n, 2n, ..., (l-1)n, 1, 1+n, 1+2n, ...$
.P2
Suppose that two blocks $w$ and $w+1$ are written to the
Fileserver with an empty cache.  Suppose further that $w+1~%~l~≠~0$.
Then blocks $w$ and $w+1$ map to disk blocks $c$ and
$c+"CEPERBK"$.  With a block size of 8192 bytes, current
Fileserver parameters and 512 byte disk sectors, this works
out to 1072 sectors between “sequential” blocks.
.PP
With disk drives of the same era as the original Fileserver,
disk transfer rates were limited by hardware buffer sizes and
interface bandwidth[5].  Assuming a transfer rate of 1MB/s and
a seek time of 15ms, it would take 8ms to transfer 8192 bytes
from the disk and less than 15ms to seek to another track or
about 347KB/s.
On modern SATA drives, it would take 26µs to transfer 8192
bytes from the disk and up to 9ms to seek.  this would only
yield 890KB/s.  During testing about 2MB/s was observed.  If
this same ratio of calculated versus actual seek time were
to hold for older drives, the older drives would operate at
near rated bandwidth.
.PP
When the formula was changed to 
.P1
    n = w % rows
    c = column*CEPERBK + n,
.P2
the blocks are linearized onto disk in the following order
.P1
    $0, 1, ..., l-1, l, l+1, ...$,
.P2
changing from row- to column-major ordering, performance
increased to ~25MB/s.  Note that not caching blocks in state
.CW Cread
insures that $w$ and $w+1$ will be stored sequentially on
disk, as
.CW column
will be the same for $w$ and $w+1$ unless $w+1~%~rows~=~0$.
However, in this case the blocks will also be stored
sequentially because row $r$ and row $r+1$ are also
sequential.
.NH
Assumptions Redux
.LP
.DS I
If a cat can kill a rat in a minute, how long would it be killing 60,000 rats?
Ah, how long, indeed!  My private opinion is that the rats would kill the cat.
.br
	– Lewis Carroll
.DE
The Fileserver's read-ahead system consists of a queue of
blocks to be read and a set of processes which read them
into the cache.  Although the original paper on the
Fileserver only lists one
.CW rah
process, the earliest Fileserver at the Labs' WORM started four.
The Fourth Edition Fileserver again started one
.CW rah
process but attempted to sort the blocks by 
.CW w-\fRaddress\fR
before processing.  This approach probably makes sense
on slow, partitioned disks.  However, it has the disadvantage
of processing blocks serially.  The more parallelism one can
achieve among or within the Fileserver's devices, the greater
the performance penalty of the sequential approach.
.PP
To test this idea, a 1GB file was created on the Fileserver
on AoE storage.  The AoE driver has a maximum of 24
outstanding frames per target.  After rebooting the
Fileserver to flush the Block Cache, it took 25.5s to read
the file.  Subsequent reads took and average of 13.72s.
After changing the read-ahead algorithm to use 10
independent
.CW rah
processes, the test was rerun.  It took 15.74s to read the
file.  Increasing the number of
.CW rah
processes to 20 reduced the uncached read time to 13.75s,
the same as the cached read time.  Two concurrent readers
can each read the entire file in 15.17s, so the throughput
appears to be limited by
.CW 9P/IL
latency.
.NH
Core Improvements
.LP
The
.CW port
directory underwent some housecleaning.  The
.CW 9p1
protocol was removed.  The console code was rewritten to use
the
.CW 9p2
code.  The time zone code was replaced with the offset pairs
from the CPU kernel to allow for arbitrary time zones.  A CEC
console was added to allow access without a serial console.
.PP
More significantly,
.CW Lock s
were changed from queueing locks to spin locks.  Since a
significant use of spin locks is to lock queues to add work
and wake consumers,
.CW unlock
reschedules if the current process no longer holds any locks
and has woken processes while it held locks.  Also, the
scheduler takes care not to preempt a process with locks
held.  This improved the throughput of single-threaded reads
by 25%.  These ideas were taken from the CPU kernel.
.PP
Networking was changed to allow interfaces with jumbo
MTUs.  This is not currently used by the IL code as it has
no MTU discovery mechanism.  
.NH
PC Architecture Improvements
.LP
By far the largest change in the PC architecture was
to memory handling.  The primary goal was to be able to
handle most of the bottom 4GB of memory.  Thus
the definition of 
.CW KZERO
needed to be changed.  The PC port inherited its memory
layout from the MIPS port.  On the MIPS processor, the high
bit indicated kernel mode.  Thus Fileserver memory was mapped from
.CW 0x80000000
to the top of memory.  Converting between a physical and
virtual address was done by inverting the high bit.  While
simple, this scheme allows for a maximum of only 2GB. 
Lowering
.CW KZERO
to
.CW 0x30000000
and mapping PCI space to
.CW 0x20000000
allows for
.CW 3328MB
memory.
.PP
Unfortunately, being able to recognize more memory puts us
in greater danger of running into PCI space while sizing
memory, so another method is needed.  A BIOS
.CW 0xe820
scan was chosen.  Unfortunately, the processor must be
in Real mode to perform the scan and the processor is
already in Protected mode when the Fileserver kernel
is started.  So, Instead of switching back to real mode,
.CW 9load
was modified to perform the scan before turning on
paging[8].
.PP
Surprisingly, the preceding changes were not enough
to enable more memory.  The Fileserver faulted when
building page tables.  It turned out this is because
the 4MB temporary pagetables built by
.CW 9load
were not enough.  The BIOS scan of the testing machine
yielded 3326MB of accessible memory.  This would require
3.25MB of page tables.  Since the bottom megabyte of memory
is unusable, we don't have any room left for the kernel.
The solution was to use 4MB pages.  This eliminates the need
for page tables, as the 1024-entry page directory has enough
space to map 4GB of memory.
.PP
On 64-bit processors, it would be relatively easy to fill in
more memory from above 4GB by using the 40-bit extensions
to 4MB pages.
.NH
The AoE Driver
.DS I
If you were plowing a field what would you rather use, 2 strong oxen or
1024 chickens? 
.br
	– Seymour Cray
.DE
.LP
This is the Fileserver's raison d'être.  The AoE driver is based on
the Plan 9 driver.  It is capable of sending jumbo or standard
AoE frames.  It allows up to 24 outstanding frames per target.
It also allows a many-to-many relationship between local interfaces
and target interfaces. 
.PP
When the AoE driver gets an I/O request, a 
.CW Srb
structure is allocated with
.CW mballoc .
Then the request is chopped up into 
.CW Frame
structures as available  Each is sized to MTU of the chosen link.  A
link is chosen round-robin fashion first among local
interfaces which can see the target and then among the
target's MAC addresses.  MTUs may be freely mixed.  The
frames are sent and the number of outstanding frames is
appropriately incremented.  The driver then sleeps on the
.CW Srb .
When awoken, the process is repeated until all the bytes in
the request have been received.
.PP
When an AoE frame is received that corresponds to I/O,
the frame is copied into the buffer of the
.CW Srb
and the number of outstanding frames is decremented.  If
there are no outstanding frame remaining, the
.CW Srb
is woken.
.PP
Since the Myricom 10Gbe cards have an MTU of 9000 bytes,
an entire 8192 byte block and the AoE header fit into a single
frame.  Thus sequential read performance depends on
frame latency.  Performance was measured with a process running
the following code
.P1
static void
devcopy(Dcopy *d)
{
	Iobuf *b;

	for(d->p = d->start; d->p < d->lim; d->p++){
		b = getbuf(d->from, d->p, Bread);
		if(b == 0)
			continue;
		putbuf(b);
	}
}
.P2
The latency for an frame with 8192 data bytes
is 79µs giving 12,500pps or 103MB/s while two concurrent reads
yield 201MB/s.  Testing beyond this level of performance has
not been performed.
.NH
System Performance
.LP
I measured both latency and throughput
of reading and writing bytes between two processes
for a number of different paths.
2007 measurements were made using an
.I SR1521
AoE target, an Intel Xeon-5000-based cpu server with
a 1.6Ghz processor and a Xeon-5000-based Fileserver
with a 3.0Ghz processor.  1993 measurements are
from [6].
The latency is measured as the round trip time
for a byte sent from one process to another and
back again.
Throughput is measured using 16k writes from
one process to another.
.ps -2
.DS C
.TS
box, tab(:);
c s s s s
c | c | c | c | c
a | n | n | n | n.
Table 1 – Performance
_
test:93 throughput:93 latency:07 throughput:07 latency
:MB/s:µs:MB/s:µs
_
pipes:8.15:255:2500:19
_
IL/ether:1.02:1420:78:72
_
URP/Datakit:0.22:1750:N/A\&:N/A\&
_
Cyclone; AoE:3.2:375:≥250:49
.TE
.DE
.NL
.LP
Random I/O was not tested for two reasons.  First, ~3GB of
recient reads and writes are stored in the Block Cache and
when new or newly modified files are reread from the cache,
they are reread sequentially.  It is expected that the
working set of the Fileserver fit in the Block Cache.
Second, since a single IL connection is latency limited,
reads of highly fragmented files like
.CW /sys/log/auth
from the WORM are not meaningfully slower (59MB/s) than
reads from the cache (62MB/s).
.NH
Discussion
.LP
Decoupling storage from the Fileserver with AoE allows for
automatic offsite backup, affords good availability,
scalablity and performance.  The Fileserver is not involved
in storage management.  It is possible to grow the existing
WORM to 9TB without restarting the fileserver.  By
reconfiguring the Fileserver, essentially unlimited storage
may be added.
.LP
The size of the WORM and Block Cache have scaled by a factor
of 1000 since [3] and single IL connections have scaled by a
factor of 200 since [7].  The Block Cache is currently at a
practical maximum for a kernel with 32-bit memory addresses.
A kernel with 64-bit memory addresses in the next logical
step.
.LP
The disk cache has not been scaled to the same extent as the
increased number of cache buckets put more pressure on the
Block Cache and would not provide much benefit.  With the
.CW conf.fastworm
option, the cache only need to be large enough to hold the
free list and any blocks in state
.CW dirty
or
.CW write .
Eliminating the cache device may make sense in the future.
The cache device could be replaced with address of the
current Superblock.  Addresses below the current Superblock
would be read only.  The disadvantage to such a scheme is
that the dump processes gives the (unused) opportunity to
optimize the ordering of
.CW w-address es.
.NH
References
.IP [1]
K. Thompson, “The Plan 9 File Server”,
Plan 9 Programmer's Manual, Second Edition,
volume 2, AT&T Bell Laboratories, Murry Hill, NJ,
1995.
.IP [2]
K. Thompson, G. Collyer,
“The 64-Bit Standalone Plan 9 File Server”,
Plan 9 Programmer's Manual, Fourth Edition,
volume 2, AT&T Bell Laboratories, Murry Hill, NJ, 2002.
.IP [3]
S. Quinlan, “A Cached WORM File System”,
\f2Software — Practice and Experience\f1, 
volume 21, number 12, pp. 1289—99.
.IP [4]
S. Hopkins, B. Coile, “ATA over Ethernet”,
published online at
http://www.coraid.com/documents/AoEr10.txt
.IP [5]
A. Tanenbaum
\f2Operating Systems, design and implementation\f1,
Prentice Hall, Englewood Cliffs, New Jersey, 1987, p. 272.
.IP [6]
Diskless Fileserver source code at /n/sources/contrib/quanstro/src/myfs.
.IP [7]
D. Presotto, P. Winterbottom,
The Organization of Networks in Plan 9
\f2Proc. of the Winter 1993 USENIX Conf\f1., pp. 271-280, San Diego, CA
.IP [8]
Modified 9load source code at /n/sources/contrib/quanstro/src/9loadaoe.
