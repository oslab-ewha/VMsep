# VMsep (VM-separated commit on jbd2/ext4)

- Nested ext4 filesystems on both guest and host machine can incur excessively frequent journal commits.
- VMsep can dramatically reduce write traffic by managing modified file blocks from each guest as a separate list and splitting the running transaction list into two sub-transactions.
- Filebench and IOzone benchmarks show that VMsep improves the I/O throughput by 19.5% on average and up to 64.2% over existing systems.

## Build
- VMsep source came from linux 4.13.0-45
- adjust `include/{linux,trace}` pointing to VMsep's include
- just make
- After build done, you have two kernel modules in `kernel/fs/{jbd2/jbd2_vmsep.ko,ext4/ext4_vmsep.ko}`
