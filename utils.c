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
#include <limits.h>
#include <unistd.h>
#include <stdbool.h>
#include <termios.h>
#include <time.h>
#include "utils.h"

bool display_prompt(void)
{
    char ch;
    bool exit_program;

    struct termios current_term;
    struct termios new_term;

    /* Get current terminal settings and save them in new_term. */
    tcgetattr(STDIN_FILENO, &current_term);
    new_term = current_term;

    // Disable so-called canonical and input echo modes
    new_term.c_lflag &= ~(ICANON | ECHO);

    /* Read at least one character without timeout. */
    new_term.c_cc[VMIN] = 1;
    new_term.c_cc[VTIME] = 0;

    tcsetattr(STDIN_FILENO, TCSANOW, &new_term);

    fprintf(stderr, "WARNING: All data on the target disk will be destroyed!\nContinue? (y/n):");

    while (1) {
        ch = getchar();

        if (ch == 'y' || ch == 'Y') {
            exit_program = false;
            break;
        } else if (ch == 'n' || ch == 'N') {
            exit_program = true;
            break;
        }
    }

    /* Restore terminal settings. */
    tcsetattr(STDIN_FILENO, TCSANOW, &current_term);
    fprintf(stderr,"%c\n", ch);

    return exit_program;
}

utils_check_t get_size_in_bytes(const char *str_size, unsigned int *value)
{
    char *unit_suffix = NULL;

    *value = strtol(str_size, &unit_suffix, 10);

    if (*value > 0 && *unit_suffix == '\0') {
        return UTILS_CHECK_OK;
    } else if (*value == 0 || *(unit_suffix + 1) != '\0') {
        return UTILS_CHECK_ERR_NAN;
    } else {
        switch (*unit_suffix) {
            case 'k':
            case 'K':
                *value *= 1024;
                break;
            case 'm':
            case 'M':
                *value *= 1048576;
                break;
            default:
                return UTILS_CHECK_ERR_UNKNOWN_UNIT;
        }
    }

    return UTILS_CHECK_OK;
}

utils_check_t str_to_uint(const char *str_value, unsigned int *value)
{
    char *strtol_endptr = NULL;

    *value = strtol(str_value, &strtol_endptr, 10);

    return (*value <= 0 || *strtol_endptr != '\0') ? UTILS_CHECK_ERR_NAN : UTILS_CHECK_OK;
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
            /* Smooth a bit bytes/s to make ETA less jumpy. */
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

void fill_random_data(char *ptr_data, unsigned int data_size)
{

    srand(time(NULL));

    for (unsigned int i = 0; i < data_size; i++) {
        /* Generate random ASCII character. */
        *(ptr_data + i) = rand() % 128;
    }
}

