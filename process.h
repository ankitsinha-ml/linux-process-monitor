
#ifndef PROCESS_H
#define PROCESS_H

#include <sys/types.h>
#include <stddef.h>

typedef struct {
    pid_t pid;
    char name[256];
    char state;
    unsigned long rss_kb;
    unsigned long long cpu_time_ticks;
    double cpu_usage;
} ProcessInfo;

int read_processes(ProcessInfo **processes, size_t *count);

unsigned long long read_total_cpu_ticks(void);

#endif
