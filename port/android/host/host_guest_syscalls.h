/*
HOST_GUEST_SYSCALLS.H

The numbers of the system calls the guest makes (host_syscall.c). Both
guests are built against musl's AArch64 system call interface
(port/android/guest/libc/arch/arm64_32): its numbers, and the AArch64
kernel's structures. The 64-bit app passes most calls straight to its
kernel, whose numbers they are; the 32-bit app (armeabi-v7a) translates
each one. Taken from that arch's bits/syscall.h.in.
*/

#ifndef __HOST_GUEST_SYSCALLS_H
#define __HOST_GUEST_SYSCALLS_H

#define GUEST_SYS_getcwd 17
#define GUEST_SYS_epoll_pwait 22
#define GUEST_SYS_dup 23
#define GUEST_SYS_dup3 24
#define GUEST_SYS_fcntl 25
#define GUEST_SYS_ioctl 29
#define GUEST_SYS_flock 32
#define GUEST_SYS_mkdirat 34
#define GUEST_SYS_unlinkat 35
#define GUEST_SYS_renameat 38
#define GUEST_SYS_statfs 43
#define GUEST_SYS_fstatfs 44
#define GUEST_SYS_ftruncate 46
#define GUEST_SYS_faccessat 48
#define GUEST_SYS_chdir 49
#define GUEST_SYS_fchmod 52
#define GUEST_SYS_fchmodat 53
#define GUEST_SYS_openat 56
#define GUEST_SYS_close 57
#define GUEST_SYS_pipe2 59
#define GUEST_SYS_getdents64 61
#define GUEST_SYS_lseek 62
#define GUEST_SYS_read 63
#define GUEST_SYS_write 64
#define GUEST_SYS_readv 65
#define GUEST_SYS_writev 66
#define GUEST_SYS_pread64 67
#define GUEST_SYS_pwrite64 68
#define GUEST_SYS_preadv 69
#define GUEST_SYS_pwritev 70
#define GUEST_SYS_pselect6 72
#define GUEST_SYS_ppoll 73
#define GUEST_SYS_readlinkat 78
#define GUEST_SYS_newfstatat 79
#define GUEST_SYS_fstat 80
#define GUEST_SYS_fsync 82
#define GUEST_SYS_fdatasync 83
#define GUEST_SYS_utimensat 88
#define GUEST_SYS_exit 93
#define GUEST_SYS_exit_group 94
#define GUEST_SYS_waitid 95
#define GUEST_SYS_set_tid_address 96
#define GUEST_SYS_futex 98
#define GUEST_SYS_nanosleep 101
#define GUEST_SYS_getitimer 102
#define GUEST_SYS_setitimer 103
#define GUEST_SYS_timer_create 107
#define GUEST_SYS_timer_gettime 108
#define GUEST_SYS_timer_settime 110
#define GUEST_SYS_clock_gettime 113
#define GUEST_SYS_clock_getres 114
#define GUEST_SYS_clock_nanosleep 115
#define GUEST_SYS_sched_yield 124
#define GUEST_SYS_kill 129
#define GUEST_SYS_tkill 130
#define GUEST_SYS_tgkill 131
#define GUEST_SYS_sigaltstack 132
#define GUEST_SYS_rt_sigaction 134
#define GUEST_SYS_rt_sigprocmask 135
#define GUEST_SYS_rt_sigtimedwait 137
#define GUEST_SYS_times 153
#define GUEST_SYS_uname 160
#define GUEST_SYS_getrlimit 163
#define GUEST_SYS_getrusage 165
#define GUEST_SYS_umask 166
#define GUEST_SYS_gettimeofday 169
#define GUEST_SYS_getpid 172
#define GUEST_SYS_getppid 173
#define GUEST_SYS_getuid 174
#define GUEST_SYS_geteuid 175
#define GUEST_SYS_getgid 176
#define GUEST_SYS_getegid 177
#define GUEST_SYS_gettid 178
#define GUEST_SYS_sysinfo 179
#define GUEST_SYS_sendmsg 211
#define GUEST_SYS_recvmsg 212
#define GUEST_SYS_brk 214
#define GUEST_SYS_munmap 215
#define GUEST_SYS_mremap 216
#define GUEST_SYS_clone 220
#define GUEST_SYS_execve 221
#define GUEST_SYS_mmap 222
#define GUEST_SYS_mprotect 226
#define GUEST_SYS_madvise 233
#define GUEST_SYS_wait4 260
#define GUEST_SYS_prlimit64 261
#define GUEST_SYS_renameat2 276
#define GUEST_SYS_getrandom 278
#define GUEST_SYS_membarrier 283
#define GUEST_SYS_statx 291
#define GUEST_SYS_clone3 435


#ifdef __aarch64__
/* the 64-bit app's kernel is the guest's: the same numbers */
#include <sys/syscall.h>
_Static_assert(GUEST_SYS_getcwd == SYS_getcwd, "getcwd");
_Static_assert(GUEST_SYS_epoll_pwait == SYS_epoll_pwait, "epoll_pwait");
_Static_assert(GUEST_SYS_dup == SYS_dup, "dup");
_Static_assert(GUEST_SYS_dup3 == SYS_dup3, "dup3");
_Static_assert(GUEST_SYS_fcntl == SYS_fcntl, "fcntl");
_Static_assert(GUEST_SYS_ioctl == SYS_ioctl, "ioctl");
_Static_assert(GUEST_SYS_flock == SYS_flock, "flock");
_Static_assert(GUEST_SYS_mkdirat == SYS_mkdirat, "mkdirat");
_Static_assert(GUEST_SYS_unlinkat == SYS_unlinkat, "unlinkat");
_Static_assert(GUEST_SYS_renameat == SYS_renameat, "renameat");
_Static_assert(GUEST_SYS_statfs == SYS_statfs, "statfs");
_Static_assert(GUEST_SYS_fstatfs == SYS_fstatfs, "fstatfs");
_Static_assert(GUEST_SYS_ftruncate == SYS_ftruncate, "ftruncate");
_Static_assert(GUEST_SYS_faccessat == SYS_faccessat, "faccessat");
_Static_assert(GUEST_SYS_chdir == SYS_chdir, "chdir");
_Static_assert(GUEST_SYS_fchmod == SYS_fchmod, "fchmod");
_Static_assert(GUEST_SYS_fchmodat == SYS_fchmodat, "fchmodat");
_Static_assert(GUEST_SYS_openat == SYS_openat, "openat");
_Static_assert(GUEST_SYS_close == SYS_close, "close");
_Static_assert(GUEST_SYS_pipe2 == SYS_pipe2, "pipe2");
_Static_assert(GUEST_SYS_getdents64 == SYS_getdents64, "getdents64");
_Static_assert(GUEST_SYS_lseek == SYS_lseek, "lseek");
_Static_assert(GUEST_SYS_read == SYS_read, "read");
_Static_assert(GUEST_SYS_write == SYS_write, "write");
_Static_assert(GUEST_SYS_readv == SYS_readv, "readv");
_Static_assert(GUEST_SYS_writev == SYS_writev, "writev");
_Static_assert(GUEST_SYS_pread64 == SYS_pread64, "pread64");
_Static_assert(GUEST_SYS_pwrite64 == SYS_pwrite64, "pwrite64");
_Static_assert(GUEST_SYS_preadv == SYS_preadv, "preadv");
_Static_assert(GUEST_SYS_pwritev == SYS_pwritev, "pwritev");
_Static_assert(GUEST_SYS_pselect6 == SYS_pselect6, "pselect6");
_Static_assert(GUEST_SYS_ppoll == SYS_ppoll, "ppoll");
_Static_assert(GUEST_SYS_readlinkat == SYS_readlinkat, "readlinkat");
_Static_assert(GUEST_SYS_newfstatat == SYS_newfstatat, "newfstatat");
_Static_assert(GUEST_SYS_fstat == SYS_fstat, "fstat");
_Static_assert(GUEST_SYS_fsync == SYS_fsync, "fsync");
_Static_assert(GUEST_SYS_fdatasync == SYS_fdatasync, "fdatasync");
_Static_assert(GUEST_SYS_utimensat == SYS_utimensat, "utimensat");
_Static_assert(GUEST_SYS_exit == SYS_exit, "exit");
_Static_assert(GUEST_SYS_exit_group == SYS_exit_group, "exit_group");
_Static_assert(GUEST_SYS_waitid == SYS_waitid, "waitid");
_Static_assert(GUEST_SYS_set_tid_address == SYS_set_tid_address, "set_tid_address");
_Static_assert(GUEST_SYS_futex == SYS_futex, "futex");
_Static_assert(GUEST_SYS_nanosleep == SYS_nanosleep, "nanosleep");
_Static_assert(GUEST_SYS_getitimer == SYS_getitimer, "getitimer");
_Static_assert(GUEST_SYS_setitimer == SYS_setitimer, "setitimer");
_Static_assert(GUEST_SYS_timer_create == SYS_timer_create, "timer_create");
_Static_assert(GUEST_SYS_timer_gettime == SYS_timer_gettime, "timer_gettime");
_Static_assert(GUEST_SYS_timer_settime == SYS_timer_settime, "timer_settime");
_Static_assert(GUEST_SYS_clock_gettime == SYS_clock_gettime, "clock_gettime");
_Static_assert(GUEST_SYS_clock_getres == SYS_clock_getres, "clock_getres");
_Static_assert(GUEST_SYS_clock_nanosleep == SYS_clock_nanosleep, "clock_nanosleep");
_Static_assert(GUEST_SYS_sched_yield == SYS_sched_yield, "sched_yield");
_Static_assert(GUEST_SYS_kill == SYS_kill, "kill");
_Static_assert(GUEST_SYS_tkill == SYS_tkill, "tkill");
_Static_assert(GUEST_SYS_tgkill == SYS_tgkill, "tgkill");
_Static_assert(GUEST_SYS_sigaltstack == SYS_sigaltstack, "sigaltstack");
_Static_assert(GUEST_SYS_rt_sigaction == SYS_rt_sigaction, "rt_sigaction");
_Static_assert(GUEST_SYS_rt_sigprocmask == SYS_rt_sigprocmask, "rt_sigprocmask");
_Static_assert(GUEST_SYS_rt_sigtimedwait == SYS_rt_sigtimedwait, "rt_sigtimedwait");
_Static_assert(GUEST_SYS_times == SYS_times, "times");
_Static_assert(GUEST_SYS_uname == SYS_uname, "uname");
_Static_assert(GUEST_SYS_getrlimit == SYS_getrlimit, "getrlimit");
_Static_assert(GUEST_SYS_getrusage == SYS_getrusage, "getrusage");
_Static_assert(GUEST_SYS_umask == SYS_umask, "umask");
_Static_assert(GUEST_SYS_gettimeofday == SYS_gettimeofday, "gettimeofday");
_Static_assert(GUEST_SYS_getpid == SYS_getpid, "getpid");
_Static_assert(GUEST_SYS_getppid == SYS_getppid, "getppid");
_Static_assert(GUEST_SYS_getuid == SYS_getuid, "getuid");
_Static_assert(GUEST_SYS_geteuid == SYS_geteuid, "geteuid");
_Static_assert(GUEST_SYS_getgid == SYS_getgid, "getgid");
_Static_assert(GUEST_SYS_getegid == SYS_getegid, "getegid");
_Static_assert(GUEST_SYS_gettid == SYS_gettid, "gettid");
_Static_assert(GUEST_SYS_sysinfo == SYS_sysinfo, "sysinfo");
_Static_assert(GUEST_SYS_sendmsg == SYS_sendmsg, "sendmsg");
_Static_assert(GUEST_SYS_recvmsg == SYS_recvmsg, "recvmsg");
_Static_assert(GUEST_SYS_brk == SYS_brk, "brk");
_Static_assert(GUEST_SYS_munmap == SYS_munmap, "munmap");
_Static_assert(GUEST_SYS_mremap == SYS_mremap, "mremap");
_Static_assert(GUEST_SYS_clone == SYS_clone, "clone");
_Static_assert(GUEST_SYS_execve == SYS_execve, "execve");
_Static_assert(GUEST_SYS_mmap == SYS_mmap, "mmap");
_Static_assert(GUEST_SYS_mprotect == SYS_mprotect, "mprotect");
_Static_assert(GUEST_SYS_madvise == SYS_madvise, "madvise");
_Static_assert(GUEST_SYS_wait4 == SYS_wait4, "wait4");
_Static_assert(GUEST_SYS_prlimit64 == SYS_prlimit64, "prlimit64");
_Static_assert(GUEST_SYS_renameat2 == SYS_renameat2, "renameat2");
_Static_assert(GUEST_SYS_getrandom == SYS_getrandom, "getrandom");
_Static_assert(GUEST_SYS_membarrier == SYS_membarrier, "membarrier");
_Static_assert(GUEST_SYS_statx == SYS_statx, "statx");
_Static_assert(GUEST_SYS_clone3 == SYS_clone3, "clone3");
#endif

#endif
