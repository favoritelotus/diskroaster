/*
 * Copyright (c) 2026, Pavel Golubinskiy
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without modification,
 * are permitted provided that the following conditions are met:
 *
 * 1. Redistributions of source code must retain the above copyright notice,
 *    this list of conditions and the following disclaimer.
 *
 * 2. Redistributions in binary form must reproduce the above copyright notice,
 *    this list of conditions and the following disclaimer in the documentation
 *    and/or other materials provided with the distribution.
 *
 * 3. Neither the name of the copyright holder nor the names of its
 *    contributors may be used to endorse or promote products derived
 *    from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDER AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT,
 * INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING,
 * BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA,
 * OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
 * WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY
 * OF SUCH DAMAGE.
 */


#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

#if defined(__linux__)
    #include <sys/ioctl.h>
    #include <linux/fs.h>
#elif defined(__FreeBSD__)
    #include <sys/disk.h>
#endif

#include "disk.h"

diskdev_check_t disk_device_check(const char *device_name)
{
    struct stat st;

    if (stat(device_name, &st) == -1)
        return DISKDEV_CHECK_ERR_STAT;

    /* FreeBSD uses character device nodes for disk devices. */
#if defined(__FreeBSD__)
    if ((st.st_mode & S_IFMT) != S_IFCHR)
        return DISKDEV_CHECK_ERR_NOT_DISK;
#else
    if ((st.st_mode & S_IFMT) != S_IFBLK)
        return DISKDEV_CHECK_ERR_NOT_DISK;
#endif

    return DISKDEV_CHECK_OK;
}

diskdev_check_t get_disk_sector_size(const char* device_name, unsigned int *sector_size)
{
    int fd;
    unsigned long ioctl_op;

    if ((fd = open(device_name, O_RDONLY)) == -1)
        return DISKDEV_CHECK_ERR_OPEN;

#if defined(__linux__)
    ioctl_op = BLKSSZGET;
#elif defined(__FreeBSD__)
    ioctl_op = DIOCGSECTORSIZE;
#endif

    if (ioctl(fd, ioctl_op, sector_size) == -1) {
        close(fd);
        return DISKDEV_CHECK_ERR_IOCTL;
    }

    close(fd);

    return DISKDEV_CHECK_OK;
}

diskdev_check_t get_disk_size(const char* device_name, off_t *disk_size)
{
    int fd;

    if ((fd = open(device_name, O_RDONLY)) == -1)
        return DISKDEV_CHECK_ERR_OPEN;

    if ((*disk_size = lseek(fd, -1, SEEK_END)) == -1)
        return DISKDEV_CHECK_ERR_LSEEK;

    *disk_size += 1;

    close(fd);

    return DISKDEV_CHECK_OK;
}

off_t get_disk_segment_size(off_t disk_size, int blocksize, int num_segments)
{
    int remainder;
    off_t disk_segment_size = disk_size / num_segments;

    /* Calculate misalignment to align disk_segment_size to blocksize. */
    remainder = disk_segment_size % blocksize;

    if (remainder > 0 ) {
        disk_segment_size -= remainder;
        disk_segment_size += blocksize;
    }

    return disk_segment_size;
}

