
#define _POSIX_C_SOURCE 200809L

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <termios.h>
#include <sys/select.h>
#include <time.h>
#include <signal.h>

#include "process.h"

static struct termios original_terminal;
static int terminal_configured = 0;
static volatile sig_atomic_t stop_requested = 0;

static void handle_signal(int signal_number) {
    (void)signal_number;
    stop_requested = 1;
}

static void restore_terminal(void) {
    if (terminal_configured) {
        tcsetattr(STDIN_FILENO, TCSANOW, &original_terminal);
    }
}

static int enable_keyboard_input(void) {
    struct termios new_terminal;

    if (tcgetattr(STDIN_FILENO, &original_terminal) == -1) {
        return -1;
    }

    new_terminal = original_terminal;
    new_terminal.c_lflag &= ~(ICANON | ECHO);

    if (tcsetattr(STDIN_FILENO, TCSANOW, &new_terminal) == -1) {
        return -1;
    }

    terminal_configured = 1;
    atexit(restore_terminal);
    return 0;
}

// Find the previous reading for the same process
static ProcessInfo *find_previous_process(
    ProcessInfo *previous,
    size_t previous_count,
    ProcessInfo *current
) {
    for (size_t i = 0; i < previous_count; i++) {
        if (previous[i].pid == current->pid &&
            strcmp(previous[i].name, current->name) == 0) {
            return &previous[i];
        }
    }

    return NULL;
}

// Calculate CPU usage using two consecutive readings
static void calculate_cpu_usage(
    ProcessInfo *current,
    size_t current_count,
    ProcessInfo *previous,
    size_t previous_count,
    unsigned long long cpu_delta,
    long cpu_cores
) {
    for (size_t i = 0; i < current_count; i++) {
        current[i].cpu_usage = 0.0;

        ProcessInfo *old = find_previous_process(
            previous,
            previous_count,
            &current[i]
        );

        if (old == NULL || cpu_delta == 0)
            continue;

        if (current[i].cpu_time_ticks < old->cpu_time_ticks)
            continue;

        unsigned long long process_delta =
            current[i].cpu_time_ticks - old->cpu_time_ticks;

        current[i].cpu_usage =
            ((double)process_delta / (double)cpu_delta)
            * cpu_cores * 100.0;
    }
}

// Sort by CPU usage (descending)
static int compare_cpu(const void *a, const void *b) {
    const ProcessInfo *p1 = a;
    const ProcessInfo *p2 = b;

    if (p1->cpu_usage < p2->cpu_usage) return 1;
    if (p1->cpu_usage > p2->cpu_usage) return -1;

    return 0;
}

// Sort by memory usage (descending)
static int compare_memory(const void *a, const void *b) {
    const ProcessInfo *p1 = a;
    const ProcessInfo *p2 = b;

    if (p1->rss_kb < p2->rss_kb) return 1;
    if (p1->rss_kb > p2->rss_kb) return -1;

    return 0;
}

// Display process information
static void display_processes(
    ProcessInfo *processes,
    size_t count,
    char sort_mode
) {
    printf("\033[H\033[J");

    printf("========== LINUX PROCESS MONITOR ==========\n");
    printf("Refresh interval: 1 second\n");
    printf("Sorting by: %s\n\n",
           sort_mode == 'C' ? "CPU Usage" : "Memory Usage");

    printf("%-8s %-25s %-8s %12s %12s\n",
           "PID",
           "PROCESS NAME",
           "STATE",
           "CPU (%)",
           "MEMORY (KB)");

    printf("--------------------------------------------------------------------------\n");

    size_t limit = count < 15 ? count : 15;

    for (size_t i = 0; i < limit; i++) {
        printf("%-8d %-25.25s %-8c %11.2f%% %12lu\n",
               (int)processes[i].pid,
               processes[i].name,
               processes[i].state,
               processes[i].cpu_usage,
               processes[i].rss_kb);
    }

    printf("\nTotal processes detected: %zu\n", count);
    printf("Controls: [C] CPU Sort | [M] Memory Sort | [Q] Quit\n");
    fflush(stdout);
}

// Wait up to one second while checking keyboard input
static void wait_for_input(char *sort_mode) {
    struct timespec delay = {0, 100000000L};

    for (int i = 0; i < 10 && !stop_requested; i++) {
        fd_set read_fds;
        struct timeval timeout = {0, 0};

        FD_ZERO(&read_fds);
        FD_SET(STDIN_FILENO, &read_fds);

        int ready = select(
            STDIN_FILENO + 1,
            &read_fds,
            NULL,
            NULL,
            &timeout
        );

        if (ready > 0) {
            char key;

            if (read(STDIN_FILENO, &key, 1) == 1) {
                if (key == 'c' || key == 'C') {
                    *sort_mode = 'C';
                }
                else if (key == 'm' || key == 'M') {
                    *sort_mode = 'M';
                }
                else if (key == 'q' || key == 'Q') {
                    stop_requested = 1;
                    break;
                }
            }
        }

        nanosleep(&delay, NULL);
    }
}

int main(void) {
    char sort_mode = 'C';

    if (enable_keyboard_input() != 0) {
        fprintf(stderr, "Unable to configure terminal.\n");
        return 1;
    }

    signal(SIGINT, handle_signal);
    signal(SIGTERM, handle_signal);

    ProcessInfo *previous = NULL;
    size_t previous_count = 0;

    unsigned long long previous_total_cpu =
        read_total_cpu_ticks();

    long cpu_cores = sysconf(_SC_NPROCESSORS_ONLN);

    if (cpu_cores <= 0)
        cpu_cores = 1;

    while (!stop_requested) {
        ProcessInfo *current = NULL;
        size_t current_count = 0;

        if (read_processes(&current, &current_count) != 0) {
            fprintf(stderr, "Unable to read process information.\n");
            free(previous);
            return 1;
        }

        unsigned long long current_total_cpu =
            read_total_cpu_ticks();

        unsigned long long cpu_delta = 0;

        if (current_total_cpu >= previous_total_cpu) {
            cpu_delta = current_total_cpu - previous_total_cpu;
        }

        // Step 1: Calculate CPU usage
        calculate_cpu_usage(
            current,
            current_count,
            previous,
            previous_count,
            cpu_delta,
            cpu_cores
        );

        // Step 2: Sort processes
        if (sort_mode == 'C') {
            qsort(current, current_count,
                  sizeof(ProcessInfo), compare_cpu);
        }
        else {
            qsort(current, current_count,
                  sizeof(ProcessInfo), compare_memory);
        }

        // Step 3: Display processes
        display_processes(current, current_count, sort_mode);

        // Step 4: Update previous snapshot
        free(previous);

        previous = current;
        previous_count = current_count;
        previous_total_cpu = current_total_cpu;

        // Step 5: Check keyboard input
        wait_for_input(&sort_mode);
    }

    free(previous);

    printf("\nProcess Monitor exited successfully.\n");

    return 0;
}
