diskroaster
=========

diskroaster is a multithreaded disk testing utility that writes and verifies data on a raw disk device. It is designed to stress-test hard drives and SSDs by dividing the disk into sections, writing data in parallel using multiple worker threads, and verifying the written content for integrity.

Features
--------

- Parallel disk testing using multiple worker threads
- Supports configurable block sizes
- Verifies data integrity after write
- Supports both random data and zero-fill modes
- Useful for burn-in testing, quality control, or diagnosing disk reliability

Usage
-----

    diskroaster [OPTIONS] DISK

Example:

    diskroaster -w 8 -b 32m -z /dev/sdd

This runs 8 parallel workers, writes 32MB blocks of zeros to /dev/sdd, and verifies them.

Options
-------
```
  -h              Print help and exit  
  -w <workers>    Number of parallel worker threads (default: 4)  
  -n <passes>     Number of write+verify passes to perform (default: 1)  
  -b <blocksize>  Block size for write operations (default: 4096)    
                  Supports k or m suffixes (e.g., 64k, 1m, 32m)  
  -z              Write zero-filled blocks instead of random data  
```
Warnings
--------

- This tool overwrites all data on the specified disk!
- Be absolutely sure the target (e.g., /dev/sdd) is not your system or a mounted disk.
- diskroaster allocates one memory buffer per worker thread.  Total memory usage is approximately:
`memory_used = num_workers × block_size`.
Using a large number of workers with a large block size can lead to high memory consumption and potentially cause the system to run out of memory (OOM).
Ensure your system has enough free RAM before running with aggressive settings like `-w 64 -b 64m`.
- You must run this as root to access raw devices.

Output & Verification
---------------------

Each worker writes to its own section of the disk. After writing, it reads back the data and verifies correctness block by block. Any mismatch or error will be reported.

Building
--------

To build and install:  
`git clone https://github.com/favoritelotus/diskroaster.git`  
`cd diskroaster`   
`make`  
`make install`

Tested on Linux and FreeBSD using standard POSIX make.

Author
------

Pavel Golubinskiy  
