#define _POSIX_C_SOURCE 200809L
#include "process.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dirent.h>
#include <ctype.h>
#include <unistd.h>

#define INITIAL_CAPACITY 64

// Check whether a directory name contains only digits
static int is_numeric(const char *str) {
    if (*str == '\0')
        return 0;

    while (*str) {
        if (!isdigit((unsigned char)*str))
            return 0;

        str++;
    }

    return 1;
}

// Read information about one process
static int read_process_stat(pid_t pid, ProcessInfo *proc) {

    char path[64];
    char buffer[4096];

    snprintf(path, sizeof(path), "/proc/%d/stat", (int)pid);

    FILE *fp = fopen(path, "r");

    if (fp == NULL)
        return -1;

    if (fgets(buffer, sizeof(buffer), fp) == NULL) {
        fclose(fp);
        return -1;
    }

    fclose(fp);

    // Locate the opening and closing parentheses
    char *start = strchr(buffer, '(');
    char *end = strrchr(buffer, ')');

    if (start == NULL || end == NULL || end <= start)
        return -1;

    // Extract process name
    size_t name_length = (size_t)(end - start - 1);

    if (name_length >= sizeof(proc->name))
        name_length = sizeof(proc->name) - 1;

    memcpy(proc->name, start + 1, name_length);
    proc->name[name_length] = '\0';

    proc->pid = pid;

    // Start parsing from field 3 (process state)
    char *saveptr;
    char *token = strtok_r(end + 1, " \t\n", &saveptr);

    int field = 3;
    long rss_pages = 0;

    proc->state = '?';
    proc->cpu_time_ticks = 0;
    proc->rss_kb = 0;

    while (token != NULL && field <= 24) {

        switch (field) {

            case 3:
                proc->state = token[0];
                break;

            case 14:
            case 15:
                proc->cpu_time_ticks += strtoull(token, NULL, 10);
                break;

            case 24:
                rss_pages = strtol(token, NULL, 10);
                break;
        }

        token = strtok_r(NULL, " \t\n", &saveptr);
        field++;
    }

    if (field <= 24)
        return -1;

    if (rss_pages < 0)
        rss_pages = 0;

    long page_size = sysconf(_SC_PAGESIZE);

    if (page_size <= 0)
        return -1;

    proc->rss_kb =
        (unsigned long)rss_pages *
        (unsigned long)page_size / 1024;

    proc->cpu_usage = 0.0;

    return 0;
}

// Read all currently accessible processes
int read_processes(ProcessInfo **processes, size_t *count) {

    DIR *dir = opendir("/proc");

    if (dir == NULL)
        return -1;

    size_t capacity = INITIAL_CAPACITY;
    size_t total = 0;

    ProcessInfo *list = malloc(capacity * sizeof(ProcessInfo));

    if (list == NULL) {
        closedir(dir);
        return -1;
    }

    struct dirent *entry;

    while ((entry = readdir(dir)) != NULL) {

        if (!is_numeric(entry->d_name))
            continue;

        pid_t pid = (pid_t)atoi(entry->d_name);

        ProcessInfo proc;

        // Processes can disappear while scanning /proc
        if (read_process_stat(pid, &proc) != 0)
            continue;

        if (total == capacity) {

            capacity *= 2;

            ProcessInfo *temp =
                realloc(list, capacity * sizeof(ProcessInfo));

            if (temp == NULL) {
                free(list);
                closedir(dir);
                return -1;
            }

            list = temp;
        }

        list[total++] = proc;
    }

    closedir(dir);

    *processes = list;
    *count = total;

    return 0;
}


// Read total CPU time from /proc/stat
unsigned long long read_total_cpu_ticks(void) {

    FILE *fp = fopen("/proc/stat", "r");

    if (fp == NULL)
        return 0;

    char label[16];

    unsigned long long user, nice, system;
    unsigned long long idle, iowait, irq;
    unsigned long long softirq, steal;

    int result = fscanf(fp, "%15s %llu %llu %llu %llu %llu %llu %llu %llu",
                        label,
                        &user, &nice, &system, &idle,
                        &iowait, &irq, &softirq, &steal);

    fclose(fp);

    if (result != 9 || strcmp(label, "cpu") != 0)
        return 0;

    return user + nice + system + idle +
           iowait + irq + softirq + steal;
}
