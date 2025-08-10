/*
 * Copyright (c) 2025, Pavel Golubinskiy
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

// Linux open() syscall's O_DIRECT flag requires to define _GNU_SOURCE   
#if defined(__linux__)
	#define _GNU_SOURCE
#endif

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <pthread.h>
#include <signal.h>
#include <errno.h>
#include <termios.h>

#if defined(__linux__)
	#include <linux/fs.h>
#elif defined(__FreeBSD__)
	#include <sys/disk.h>
#endif

#define PROGNAME "diskroaster"
#define PROG_VERSION "1.3.0"
#define MIN_BLOCK_SIZE 512
#define DEFAULT_BLOCK_SIZE 4096
#define DEFAULT_NUM_WORKERS 4
#define DEFAULT_NUM_PASSES 1

typedef struct worker_params_t {
        char *device_name;
        char *wr_data;
        volatile off_t offset;
        off_t num_blocks;
	off_t disk_size;
        int blocksize;
} worker_params_t;

pthread_mutex_t mutex_verified_bytes = PTHREAD_MUTEX_INITIALIZER;
pthread_mutex_t mutex_workers_run = PTHREAD_MUTEX_INITIALIZER;

pthread_t *workers_id = NULL;
pthread_attr_t tattr;

int blocksize = DEFAULT_BLOCK_SIZE;
int num_workers = DEFAULT_NUM_WORKERS;
int num_passes = DEFAULT_NUM_PASSES;
int pass;
int terminate;
int write_zeros;
int sector_size;
int skip_prompt;
volatile int workers_run;

char *device_name = NULL;
char *wr_data = NULL;
worker_params_t *worker_params = NULL;

volatile off_t verified_bytes;

void handle_sigint(int sig) 
{
	// Stop program when SIGINT is received. 	 
    	fprintf(stderr, "\nAborting...\n"); 
	workers_run = 0;	
	terminate = 1;
} 

int display_prompt(void)
{
	char ch;
	int exit_program;

	struct termios current_term;
	struct termios new_term;

	// Get current terminal settings and save them in new_term.	
	tcgetattr(STDIN_FILENO, &current_term);      
	new_term = current_term;

	// Disable so-called canonical and input echo modes
	new_term.c_lflag &= ~(ICANON | ECHO);

	// Read at least one character without timeout.
	new_term.c_cc[VMIN] = 1;
	new_term.c_cc[VTIME] = 0; 
             
	tcsetattr(STDIN_FILENO, TCSANOW, &new_term);

	fprintf(stderr, "WARNING: All data on the target disk will be destroyed!\nContinue? (y/n):");
    
	while (1) {
		ch = getchar();

		if (ch == 'y' || ch == 'Y') {
			exit_program =	0;
			break;
		} else if (ch == 'n' || ch == 'N') {
			exit_program = 1;
			break;
		}
	}
	
	// Restore terminal settings.
	tcsetattr(STDIN_FILENO, TCSANOW, &current_term);
	fprintf(stderr,"%c\n", ch);
	
	return exit_program;
}

void cleanup_resources(void)
{
	pthread_mutex_destroy(&mutex_verified_bytes);
        pthread_mutex_destroy(&mutex_workers_run);
        pthread_attr_destroy(&tattr);

        if (worker_params != NULL)
		free(worker_params);

        if (workers_id != NULL)
		free(workers_id);

	if (wr_data != NULL)
        	free(wr_data);
}

void usage(void)
{
        char *usage = 
		PROGNAME " - Multi-threaded disk testing utility, v" PROG_VERSION "\n\n"
		"Usage: " PROGNAME " [OPTIONS] DEVICE\n\n"
		"Options:\n"
		"  -h			- Print help and exit\n"
		"  -w <workers>		- Number of parallel worker threads (default: 4)\n"
		"  -n <passes>		- Number of write+verify passes to perform (default: 1)\n"
		"  -b <blocksize>	- Block size for write operations (default: 4096)\n" 
		"			  Supports k and m suffixes (e.g., 64k, 1m, 32m)\n"
		"  -y			- Skip confirmation prompt and start immediately\n"
		"			  This will destroy all data on the target disk\n"						
		"  -z			- Write zero-filled blocks instead of random data\n";

	fprintf(stderr, "%s", usage);
	exit(0);
}

void fill_rand_data(char *ptr_data, size_t data_size)
{
	int i;
	char byte;

	for (i = 0; i < data_size; i++) {
		// Generate random ASCII character.
		byte = rand() % 128;
		ptr_data[i] = byte;
	}
}


int get_size_in_bytes(const char *str_unit)
{
	int str_unit_len = strlen(str_unit);
	char unit_suffix = str_unit[str_unit_len - 1];
	int unit;

        if (unit_suffix >= '0' && unit_suffix <= '9')
		return strtol(str_unit, NULL, 10);

	switch (unit_suffix) {
		case 'k':
		case 'K':
			unit = strtol(str_unit, NULL, 10);
			unit *= 1024;
			break;
		case 'm':
		case 'M':
			unit = strtol(str_unit, NULL, 10);
			unit *= 1048576;
			break;
		default:
			return -1;
	}

	return unit;
}

off_t get_disk_size(const char* device_name)
{
	off_t disk_size;
	int fd;

	if ((fd = open(device_name, O_RDONLY)) == -1) {
		perror("open()");
		exit(EXIT_FAILURE);
	}

	if ((disk_size = lseek(fd, -1, SEEK_END)) == -1) {
                perror("lseek()");
                exit(EXIT_FAILURE);
        }

	disk_size += 1;

	close(fd);

	return disk_size;
}

unsigned int get_disk_sector_size(const char* device_name)
{
        int fd;
	unsigned int sector_size;
 
        if ((fd = open(device_name, O_RDONLY)) == -1) {
                perror("open()");
                exit(EXIT_FAILURE);
        }


#if defined(__linux__)
	if (ioctl(fd, BLKSSZGET, &sector_size) == -1) {
#elif defined(__FreeBSD__)
	if (ioctl(fd, DIOCGSECTORSIZE, &sector_size) == -1) {
#endif
		perror("ioctl()");
                exit(EXIT_FAILURE);
	}
	
	return sector_size;
}

void *worker(void *worker_params)
{
	struct worker_params_t *params = (struct worker_params_t*) worker_params;
	int fd;
	int block_counter;
	int blocksize = params->blocksize;
	off_t offset = params->offset;
	off_t num_blocks = params->num_blocks;
	off_t disk_size = params->disk_size;
	off_t current_offset;
	ssize_t written_bytes;
	char *buffer = NULL;

	if (posix_memalign((void**)&buffer, sector_size, blocksize)  != 0) {
		fprintf(stderr, "%s\n", "No free memory to allocate.");
                exit(EXIT_FAILURE);
        }

	if ((fd = open(params->device_name, O_RDWR|O_DIRECT)) == -1) {
                perror("open()");
                exit(EXIT_FAILURE);
        }

	if (lseek(fd, offset, SEEK_SET) == -1) {
                perror("lseek()");
                exit(EXIT_FAILURE);
        }

	current_offset = offset;
	for (block_counter = 0; block_counter < num_blocks; block_counter++) {
                if (verified_bytes > disk_size) {
			verified_bytes = disk_size;
                	break;
 		}

		written_bytes = write(fd, params->wr_data, blocksize);

		if (written_bytes == -1) {
			if (errno == ENOSPC) {
				break;
			} else {
				perror("write()");
                		exit(EXIT_FAILURE);
			}
        	}

		// Read back the written block.
		if (lseek(fd, -written_bytes, SEEK_CUR) == -1) {
                	perror("lseek()");
                	exit(EXIT_FAILURE);
        	}
                
		if (read(fd, buffer, written_bytes) == -1) {
			perror("read()");
                        exit(EXIT_FAILURE);
                }

		if (memcmp(params->wr_data, buffer, written_bytes) != 0) {
			fprintf(stderr, "Error verifying block at offset #: %ld\n", current_offset);
		}
   
		pthread_mutex_lock(&mutex_verified_bytes);
        	verified_bytes += written_bytes;
        	pthread_mutex_unlock(&mutex_verified_bytes);

		current_offset += written_bytes;
	}
		
	free(buffer);
	close(fd);

	// Pause briefly to allow verified_bytes to stabilize
	sleep(3);

	pthread_mutex_lock(&mutex_workers_run);
	workers_run--;
	pthread_mutex_unlock(&mutex_workers_run);

	pthread_exit(NULL);

}

void get_eta(char *eta, off_t verified_bytes, off_t disk_size)
{
        static off_t verified_bytes_prev = 0;
        off_t bps;
	static double avg_bps = 0;
        off_t bytes_left;
        long eta_secs;
	int hours;
	int minutes;
	int seconds;

	if (verified_bytes == 0) {
		eta_secs = 0;
	} else if (verified_bytes_prev == 0) {
		verified_bytes_prev = verified_bytes;
		eta_secs = 0;
	} else {
        	bps = verified_bytes - verified_bytes_prev;

		if (bps > 0) {
			// Smooth a bit bytes/s to make ETA less jumpy.
			avg_bps = (avg_bps == 0)? bps: avg_bps * 0.7 + bps * 0.3;
		}

        	bytes_left = disk_size - verified_bytes;
		eta_secs = (avg_bps > 0) ? (bytes_left / avg_bps) : 0;
        	verified_bytes_prev = verified_bytes;
	}

	hours = eta_secs / 3600;
	minutes = (eta_secs / 60) % 60;

	if (hours > 0)
        	sprintf(eta, "%dh %dm", hours, minutes);
    	else {
		seconds = eta_secs % 60;
        	sprintf(eta, "%dm %ds", minutes, seconds);
	}

        return;
}

int main(int argc, char **argv)
{

	int opt;
	int worker_counter;
	int ret;
	off_t disk_segment_size;
	off_t num_blocks;
	off_t offset;
	off_t disk_size;
	int remainder;
	char eta[9];

	struct stat st;
	mode_t device_type;

	while ((opt = getopt(argc, argv, "b:w:n:zhy")) != -1) {

		switch (opt) {
			case 'b':
				blocksize = get_size_in_bytes(optarg);
				if (blocksize < 0) {
					fprintf(stderr, "%s\n", "Unknown unit suffix");
                        		exit(EXIT_FAILURE);
				} else if (blocksize % MIN_BLOCK_SIZE != 0) {
                                        fprintf(stderr, "The block size must be a power of two.\n");
                                        exit(EXIT_FAILURE);
				}	
 
                     		break;

			case 'w':
				num_workers = strtol(optarg, NULL, 10);
				break;
			case 'n':
				num_passes = strtol(optarg, NULL, 10);
				break;
                        case 'z':
				write_zeros = 1;
				break;
			case 'y':
				skip_prompt = 1;
				break;	
			case 'h': 
			case '?':
			default:
                     		usage();
		}
	}

	if (optind >= argc)
		usage();

	device_name = argv[optind];

	if (stat(device_name, &st) == -1) {
               	perror("stat()");
		cleanup_resources();
               	exit(EXIT_FAILURE);
        }

// FreeBSD uses character device nodes for disk devices. 
#if defined(__FreeBSD__)
	device_type = S_IFCHR;
#else
	device_type = S_IFBLK;
#endif

	if ((st.st_mode & S_IFMT) != device_type) {
		fprintf(stderr, "%s is not a disk device.\n", device_name);
		cleanup_resources();
		exit(EXIT_FAILURE);
	}

	sector_size = get_disk_sector_size(device_name);

	if (blocksize < sector_size) {
		fprintf(stderr, "The block size can't be less than the disk sector size (%u).\n",
				sector_size);
		exit(EXIT_FAILURE);
	}

	if (!skip_prompt && display_prompt())
		exit(EXIT_SUCCESS);

	if (posix_memalign((void**)&wr_data, sector_size, blocksize)  != 0) {
		fprintf(stderr, "%s\n", "No free memory to allocate.");
                cleanup_resources();
                exit(EXIT_FAILURE);
        }

        (write_zeros)? memset(wr_data, 0, blocksize) : fill_rand_data(wr_data, blocksize);

	disk_size = get_disk_size(device_name);
	
	disk_segment_size = disk_size / num_workers;

	// Calculate misalignment to align disk_segment_size to blocksize.
        remainder = disk_segment_size % blocksize;
	
        if (remainder > 0 ) {
		disk_segment_size -= remainder;
		disk_segment_size += blocksize;
	}

	num_blocks = disk_segment_size / blocksize;

	workers_id = malloc(num_workers * sizeof(pthread_t));

        if (workers_id == NULL) {
		fprintf(stderr, "%s\n", "No free memory to allocate.");
		cleanup_resources();
        	exit(EXIT_FAILURE);
        }

	// Set all threads as detached threads.
	pthread_attr_init(&tattr);
	pthread_attr_setdetachstate(&tattr, PTHREAD_CREATE_DETACHED);

	worker_params = malloc(num_workers * sizeof(worker_params_t));

        if (worker_params == NULL) {
                fprintf(stderr, "%s\n", "No free memory to allocate.");
		cleanup_resources();
                exit(EXIT_FAILURE);
        }

	signal(SIGINT, handle_sigint);

	pass = 1;

	do {
		bzero(worker_params, num_workers);
		bzero(workers_id, num_workers * sizeof(pthread_t));
		offset = 0;

		// Each worker decreases workers_run by one when its job is done.
        	workers_run = num_workers;

		verified_bytes = 0;

		// Create threads for workers.
		for (worker_counter = 0; worker_counter < num_workers; worker_counter++) {

			// Set parameters to pass to a thread worker.
			worker_params[worker_counter].device_name = device_name;
			worker_params[worker_counter].wr_data = wr_data;
			worker_params[worker_counter].num_blocks = num_blocks;
			worker_params[worker_counter].blocksize = blocksize;
			worker_params[worker_counter].offset = offset;
			worker_params[worker_counter].disk_size = disk_size;
		
			ret = pthread_create(&workers_id[worker_counter], &tattr, worker, &worker_params[worker_counter]);
			if (ret != 0) {
				fprintf(stderr, "%s\n", "Error: couldn't create a thread.");
				cleanup_resources();
                		exit(EXIT_FAILURE);
        		}

			offset += disk_segment_size;
		}

		while (workers_run) {
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
	}  while (pass <= num_passes && !terminate);

	putchar('\n');
	cleanup_resources();

	return 0;
}

