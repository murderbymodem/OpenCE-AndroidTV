struct timespec { long tv_sec; long tv_nsec; };
#define CLOCK_MONOTONIC 1
int clock_gettime(int clock, struct timespec *time);
