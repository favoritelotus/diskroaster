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


/* Linux open() syscall's O_DIRECT flag requires to define _GNU_SOURCE. */
#if defined(__linux__)
    #define _GNU_SOURCE
#endif

#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "disk.h"
#include "utils.h"
#include "workers.h"

typedef struct common_worker_params_t {
    const char *device_name;
    const char *wr_data;
    off_t num_blocks;
    off_t disk_size;
    unsigned int blocksize;
    unsigned int sector_size;
} common_worker_params_t;

typedef struct worker_params_t {
    volatile off_t offset;
    common_worker_params_t *common_worker_params;
} worker_params_t;

int pthread_errno;

static bool workers_stop = false;
static pthread_mutex_t mutex_verified_bytes = PTHREAD_MUTEX_INITIALIZER;
static pthread_mutex_t mutex_workers_run = PTHREAD_MUTEX_INITIALIZER;

static pthread_t *workers_id;
static pthread_attr_t tattr;
static worker_params_t *worker_params;
static common_worker_params_t *common_worker_params;
static unsigned int num_workers;
static unsigned int workers_run;
static off_t verified_bytes;

/*
 * Internal functions' prototypes
 */

static void *worker(void*);
static inline void lock_mutex(pthread_mutex_t*);
static inline void unlock_mutex(pthread_mutex_t*);

workers_check_t init_workers(
    unsigned int n_workers,
    const char *device_name,
    off_t disk_size,
    unsigned int blocksize,
    unsigned int sector_size,
    const char* wr_data
) {
    num_workers = n_workers;

    workers_id = malloc(num_workers * sizeof(pthread_t));

    if (workers_id == NULL)
        return WORKERS_CHECK_ERR_MEM_ALLOC;

    common_worker_params = malloc(sizeof(common_worker_params_t));

    if (common_worker_params == NULL)
        return WORKERS_CHECK_ERR_MEM_ALLOC;

    worker_params = malloc(num_workers * sizeof(worker_params_t));

    if (worker_params == NULL)
        return WORKERS_CHECK_ERR_MEM_ALLOC;

    /* Set all threads as detached threads. */
    if ((pthread_errno = pthread_attr_init(&tattr)) != 0)
        return WORKERS_CHECK_ERR_PTHREAD;

    if ((pthread_errno = pthread_attr_setdetachstate(&tattr, PTHREAD_CREATE_DETACHED)) != 0)
        return WORKERS_CHECK_ERR_PTHREAD;

    /* Set common parametes for workers. */
    common_worker_params->device_name = device_name;
    common_worker_params->disk_size = disk_size;
    common_worker_params->blocksize = blocksize;
    common_worker_params->sector_size = sector_size;
    common_worker_params->wr_data = wr_data;
    common_worker_params->num_blocks = get_disk_segment_size(
            disk_size,
            blocksize,
            num_workers
    ) / blocksize;

    return WORKERS_CHECK_OK;
}

workers_check_t start_workers(void)
{

    off_t offset = 0;
    off_t disk_segment_size;

    /* Each worker decreases workers_run by one when its job is done. */
    workers_run = num_workers;

    verified_bytes = 0;

    disk_segment_size = get_disk_segment_size(
            common_worker_params->disk_size,
            common_worker_params->blocksize,
            num_workers
    );

    bzero(worker_params, num_workers * sizeof(worker_params_t));
    bzero(workers_id, num_workers * sizeof(pthread_t));

    /* Create threads for workers. */
    for (unsigned int worker_counter = 0; worker_counter < num_workers; worker_counter++) {

        worker_params[worker_counter].common_worker_params = common_worker_params;
        worker_params[worker_counter].offset = offset;

        pthread_errno = pthread_create(&workers_id[worker_counter],
                &tattr,
                worker,
                &worker_params[worker_counter]
        );

        if (pthread_errno != 0)
            return WORKERS_CHECK_ERR_PTHREAD;

        offset += disk_segment_size;
    }

    return WORKERS_CHECK_OK;
}

void stop_workers(void)
{
    workers_stop = true;

    return;
}

static void *worker(void *worker_params)
{
    struct worker_params_t *params = (struct worker_params_t*) worker_params;
    int fd;
    unsigned int blocksize = params->common_worker_params->blocksize;
    unsigned int sector_size = params->common_worker_params->sector_size;
    off_t num_blocks = params->common_worker_params->num_blocks;
    off_t disk_size = params->common_worker_params->disk_size;
    const char *device_name = params->common_worker_params->device_name;
    const char *wr_data = params->common_worker_params->wr_data;
    off_t offset = params->offset;
    off_t current_offset;
    ssize_t written_bytes;
    char *buffer = NULL;
    char error_buffer[256] = {0};
    int local_errno;

    if ((fd = open(device_name, O_RDWR|O_DIRECT)) == -1) {
        local_errno = errno;
        strerror_r(local_errno, error_buffer, sizeof(error_buffer));
        fprintf(stderr, "Can't open device: %s: %s\n", device_name,
                        error_buffer);
        cleanup_workers();
        exit(EXIT_FAILURE);
    }

    if (lseek(fd, offset, SEEK_SET) == -1) {
        local_errno = errno;
        strerror_r(local_errno, error_buffer, sizeof(error_buffer));
        fprintf(stderr, "Failed to seek on disk device: %s: %s\n", device_name,
                        error_buffer);
        cleanup_workers();
        exit(EXIT_FAILURE);
    }

    if (posix_memalign((void**)&buffer, sector_size, blocksize)  != 0) {
        fprintf(stderr, "%s\n", "No free memory to allocate.");
        cleanup_workers();
        exit(EXIT_FAILURE);
    }

    current_offset = offset;
    for (off_t block_counter = 0; block_counter < num_blocks; block_counter++) {

        if (workers_stop)
            goto exit_worker;

        if (verified_bytes > disk_size) {
            lock_mutex(&mutex_verified_bytes);
            verified_bytes = disk_size;
            unlock_mutex(&mutex_verified_bytes);
            break;
        }

        written_bytes = write(fd, wr_data, blocksize);

        if (written_bytes == -1) {
            local_errno = errno;

            if (local_errno == ENOSPC) {
                break;
            } else {
                strerror_r(local_errno, error_buffer, sizeof(error_buffer));
                fprintf(stderr, "Failed to write data to disk device: %s: %s\n", device_name,
                                error_buffer);

                free(buffer);
                cleanup_workers();
                exit(EXIT_FAILURE);
            }
        }

        /* Read back the written block for verification. */
        if (lseek(fd, -written_bytes, SEEK_CUR) == -1) {
            local_errno = errno;
            strerror_r(local_errno, error_buffer, sizeof(error_buffer));
            fprintf(stderr, "Failed to seek on disk device: %s: %s\n", device_name,
                            error_buffer);
            free(buffer);
            cleanup_workers();
            exit(EXIT_FAILURE);
        }

        if (read(fd, buffer, written_bytes) == -1) {
            local_errno = errno;
            strerror_r(local_errno, error_buffer, sizeof(error_buffer));
            fprintf(stderr, "Failed to read back written data on disk device: %s: %s\n", device_name,
                            error_buffer);
            free(buffer);
            cleanup_workers();
            exit(EXIT_FAILURE);
        }

        if (memcmp(wr_data, buffer, written_bytes) != 0) {
            fprintf(stderr, "Error verifying block at offset #: %ld\n", current_offset);
        }

        lock_mutex(&mutex_verified_bytes);
        verified_bytes += written_bytes;
        unlock_mutex(&mutex_verified_bytes);

        current_offset += written_bytes;
    }

exit_worker:
    free(buffer);
    close(fd);

    /* Pause briefly to synchronize the output statistic. */
    sleep(3);

    lock_mutex(&mutex_workers_run);
    workers_run--;
    unlock_mutex(&mutex_workers_run);

    pthread_exit(NULL);
}

static inline void lock_mutex(pthread_mutex_t *mutex)
{
    char error_buffer[256] = {0};
    int mutex_errno;

    if ((mutex_errno = pthread_mutex_lock(mutex)) != 0) {
        strerror_r(mutex_errno, error_buffer, sizeof(error_buffer));
        fprintf(stderr, "Failed to lock mutex: %s\n", error_buffer);
        cleanup_workers();
        exit(EXIT_FAILURE);
    }

    return;
}

static inline void unlock_mutex(pthread_mutex_t *mutex)
{
    char error_buffer[256] = {0};
    int mutex_errno;

    if ((mutex_errno = pthread_mutex_unlock(mutex)) != 0) {
        strerror_r(mutex_errno, error_buffer, sizeof(error_buffer));
        fprintf(stderr, "Failed to unlock mutex: %s\n", error_buffer);
        cleanup_workers();
        exit(EXIT_FAILURE);
    }

    return;
}

bool are_workers_running(void)
{
    bool workers_runing = false;

    lock_mutex(&mutex_workers_run);

    if (workers_run)
        workers_runing = true;

    unlock_mutex(&mutex_workers_run);

    return workers_runing;
}

off_t get_workers_progress(void)
{
    /*
     *  The return value from the function are the bytes verified by  all workers.
     */

    off_t vrfd_bytes;

    lock_mutex(&mutex_verified_bytes);
    vrfd_bytes = verified_bytes;
    unlock_mutex(&mutex_verified_bytes);

    return vrfd_bytes;
}

void cleanup_workers(void)
{
    pthread_mutex_destroy(&mutex_verified_bytes);
    pthread_mutex_destroy(&mutex_workers_run);
    pthread_attr_destroy(&tattr);

    if (common_worker_params->wr_data != NULL)
        free((void*)common_worker_params->wr_data);

    if (common_worker_params != NULL)
        free(common_worker_params);

    if (worker_params != NULL)
        free(worker_params);

    if (workers_id != NULL)
        free(workers_id);

    return;
}

