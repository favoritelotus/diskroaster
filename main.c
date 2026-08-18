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

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <stdbool.h>
#include <signal.h>
#include <errno.h>

#include "utils.h"
#include "disk.h"
#include "workers.h"

#define PROGNAME "diskroaster"
#define PROG_VERSION "1.4.0"
#define MIN_BLOCK_SIZE 512
#define DEFAULT_BLOCK_SIZE 4096
#define DEFAULT_NUM_WORKERS 4
#define DEFAULT_NUM_PASSES 1

bool terminate = false;

void handle_sigint(int sig)
{
    /* Explicitly cast sig to void to mute the compiler's unused variable warning. */
    (void)sig;

    /* Stop program when SIGINT is received. */
    fprintf(stderr, "\nAborting...\n");

    stop_workers();
    terminate = true;
}

void usage(void)
{
    char *usage =
    PROGNAME " - Multi-threaded disk testing utility, v" PROG_VERSION "\n\n"
    "Usage: " PROGNAME " [OPTIONS] DEVICE\n\n"
    "Options:\n"
    "  -h               - Print help and exit\n"
    "  -w <workers>     - Number of parallel worker threads (default: 4)\n"
    "  -n <passes>      - Number of write+verify passes to perform (default: 1)\n"
    "  -b <blocksize>   - Block size for write operations (default: 4096)\n"
    "                     Supports k and m suffixes (e.g., 64k, 1m, 32m)\n"
    "  -y               - Skip confirmation prompt and start immediately\n"
    "                     This will destroy all data on the target disk\n"
    "  -z               - Write zero-filled blocks instead of random data\n";

    fprintf(stderr, "%s", usage);
}

int main(int argc, char **argv)
{

    int opt;
    off_t disk_size;
    char eta[9];
    utils_check_t result;
    unsigned int blocksize = DEFAULT_BLOCK_SIZE;
    unsigned num_workers = DEFAULT_NUM_WORKERS;
    unsigned num_passes = DEFAULT_NUM_PASSES;
    unsigned int pass;
    unsigned int sector_size;
    bool write_zeros = false;
    bool skip_prompt = false;
    char *device_name = NULL;
    char *wr_data = NULL;
    off_t verified_bytes;

    while ((opt = getopt(argc, argv, "b:w:n:zhy")) != -1) {

        switch (opt) {
            case 'b':
                result = get_size_in_bytes(optarg, &blocksize);

                if (result == UTILS_CHECK_ERR_UNKNOWN_UNIT) {
                    fprintf(stderr, "%s\n", "Unknown unit suffix set in block size.");
                    exit(EXIT_FAILURE);
                } else if (blocksize == UTILS_CHECK_ERR_NAN) {
                    fprintf(stderr, "%s\n", "Invalid block size value.");
                    exit(EXIT_FAILURE);
                } else if (blocksize % MIN_BLOCK_SIZE != 0) {
                    fprintf(stderr, "Block size is required to be a multiple of 512 bytes.\n");
                    exit(EXIT_FAILURE);
                }
                break;

            case 'w':
                result = str_to_uint(optarg, &num_workers);

                if (result == UTILS_CHECK_ERR_NAN) {
                    fprintf(stderr, "%s\n", "Invalid number of workers.");
                    exit(EXIT_FAILURE);
                }

                break;

            case 'n':
                result = str_to_uint(optarg, &num_passes);

                if (result == UTILS_CHECK_ERR_NAN) {
                    fprintf(stderr, "%s\n", "Invalid number of passes.");
                    exit(EXIT_FAILURE);
                }

                break;

            case 'z':
                write_zeros = true;
                break;

            case 'y':
                skip_prompt = true;
                break;

            case 'h':
            case '?':

            default:
                usage();
                exit(EXIT_SUCCESS);
        }
    }

    if (optind >= argc) {
        usage();
        exit(EXIT_SUCCESS);
    }

    device_name = argv[optind];

    switch (disk_device_check(device_name)) {
        case DISKDEV_CHECK_ERR_STAT:
            fprintf(stderr, "Can't access device: %s: %s\n", device_name, strerror(errno));
            exit(EXIT_FAILURE);

        case DISKDEV_CHECK_ERR_NOT_DISK:
            fprintf(stderr, "Error: %s is not a disk device.\n", device_name);
            exit(EXIT_FAILURE);

        default:
            break;
    }

    switch (get_disk_sector_size(device_name, &sector_size)) {
        case DISKDEV_CHECK_ERR_OPEN:
            fprintf(stderr, "Can't open device: %s: %s\n", device_name,
                            strerror(errno));
            exit(EXIT_FAILURE);

        case DISKDEV_CHECK_ERR_IOCTL:
            fprintf(stderr, "Can't get sector size of the device: %s: %s\n", device_name,
                            strerror(errno));
            exit(EXIT_FAILURE);

        default:
            break;

    }

    if (blocksize < sector_size) {
        fprintf(stderr, "The block size can't be less than the disk's sector size (%u).\n",
                        sector_size);
        exit(EXIT_FAILURE);
    }

    switch (get_disk_size(device_name, &disk_size)) {
        case DISKDEV_CHECK_ERR_OPEN:
            fprintf(stderr, "Can't open device: %s: %s\n", device_name,
                            strerror(errno));
            exit(EXIT_FAILURE);

        case DISKDEV_CHECK_ERR_LSEEK:
            fprintf(stderr, "Can't get disk size from the device: %s: %s\n", device_name,
                            strerror(errno));
            exit(EXIT_FAILURE);

        default:
            break;
    }

    /* Continue to perform data destructive disk testing? */
    if (!skip_prompt && display_prompt())
        exit(EXIT_SUCCESS);

    if (posix_memalign((void**)&wr_data, sector_size, blocksize)  != 0) {
        fprintf(stderr, "%s\n", "No free memory to allocate.");
            exit(EXIT_FAILURE);
    }

    (write_zeros)? memset(wr_data, 0, blocksize) : fill_random_data(wr_data, blocksize);

    signal(SIGINT, handle_sigint);

    switch (init_workers(num_workers, device_name, disk_size, blocksize, sector_size, wr_data)) {
        case WORKERS_CHECK_ERR_MEM_ALLOC:
            fprintf(stderr, "%s\n", "No free memory to allocate.");
            free(wr_data);
            exit(EXIT_FAILURE);

        case WORKERS_CHECK_ERR_PTHREAD:
            fprintf(stderr, "Error initializing workers: %s\n", strerror(pthread_errno));
            free(wr_data);
            exit(EXIT_FAILURE);

        default:
            break;
    }

    pass = 1;

    do {

        if (start_workers() == WORKERS_CHECK_ERR_PTHREAD) {
            fprintf(stderr, "Error starting workers: %s\n", strerror(pthread_errno));
            cleanup_workers();
            exit(EXIT_FAILURE);
        }

        while (are_workers_running()) {
            /* If SIGINT is received,  wait for all workers to stop. */
            if (terminate) {
                sleep(1);
                continue;
            }

            verified_bytes = get_workers_progress();

            get_eta(eta, verified_bytes, disk_size);

            fprintf(stderr, "\033[2K\rpass: %d/%d, verified: %ld MB, completed: %ld%%, ETA: %s\r",
                            pass,
                            num_passes,
                            (verified_bytes / 1024 / 1024),
                            (verified_bytes * 100) / disk_size,
                            eta);
            sleep(1);
        }

        pass++;
    } while (pass <= num_passes && terminate == false);

    putchar('\n');
    cleanup_workers();

    return 0;
}

