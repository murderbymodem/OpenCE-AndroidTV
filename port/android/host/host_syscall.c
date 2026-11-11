/*
HOST_SYSCALL.C

System calls on behalf of the guest's musl runtime (its syscall_arch.h
sends every call here).

Most calls pass straight through: the guest's pointers are valid host
addresses, and its integer arguments arrive properly extended to 64 bits.
The exceptions are

- structures whose ILP32 layout differs from the kernel's: timespec and
  timeval (the guest's time_t is 32-bit, as in the MSVC runtime), iovec;
- memory mappings, which must stay below 4 GB (host_memory.c);
- the standard output and error streams, which go to logcat;
- process exit, and calls that have no meaning for the guest (signal
  handlers, which the host owns).

The guest's calls are AArch64's (host_guest_syscalls.h): its numbers, and
the AArch64 kernel's structures. The 64-bit app passes the others through
as they are. The 32-bit app (armeabi-v7a) makes each through bionic
instead (arm_passthrough), with 32-bit ARM's numbers, its 64-bit file
offsets and its struct stat, which the guest receives in AArch64's layout.
*/

#include "host.h"
#include "host_guest_syscalls.h"

#include <errno.h>
#include <fcntl.h>
#include <linux/futex.h>
#include <poll.h>
#include <pthread.h>
#include <signal.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <sys/uio.h>
#include <sys/file.h>
#include <sys/random.h>
#include <sys/resource.h>
#include <sys/utsname.h>
#include <dirent.h>
#include <sched.h>
#include <stdio.h>
#include <time.h>
#include <unistd.h>

#include <android/log.h>

/* the guest's structures */
struct guest_timespec
{
	int32_t seconds;
	int32_t nanoseconds;
};

struct guest_iovec
{
	uint32_t base;
	uint32_t length;
};

#define GUEST(type, value) ((type)(uintptr_t)(uint32_t)(value))

static int timespec_in(uint64_t address, struct timespec *result)
{
	const struct guest_timespec *value = GUEST(const struct guest_timespec *, address);

	if (!address)
		return 0;
	result->tv_sec = value->seconds;
	result->tv_nsec = value->nanoseconds;
	return 1;
}

static void timespec_out(uint64_t address, const struct timespec *value)
{
	struct guest_timespec *result = GUEST(struct guest_timespec *, address);

	if (!address)
		return;
	result->seconds = (int32_t)value->tv_sec;
	result->nanoseconds = (int32_t)value->tv_nsec;
}

static long result_of(long value)
{
	return value == -1 ? -errno : value;
}

/* ---------- standard output and error */

struct log_stream
{
	char line[1024];
	size_t length;
};

static struct log_stream log_streams[2];
static pthread_mutex_t log_lock = PTHREAD_MUTEX_INITIALIZER;

static void log_bytes(int fd, const char *bytes, size_t size)
{
	struct log_stream *stream = &log_streams[fd == 2];
	size_t index;

	pthread_mutex_lock(&log_lock);
	for (index = 0; index < size; index++)
	{
		char c = bytes[index];

		if (c == '\n' || stream->length == sizeof(stream->line) - 1)
		{
			stream->line[stream->length] = 0;
			__android_log_write(fd == 2 ? ANDROID_LOG_WARN : ANDROID_LOG_INFO, "halo", stream->line);
			stream->length = 0;
			if (c == '\n')
				continue;
		}
		stream->line[stream->length++] = c;
	}
	pthread_mutex_unlock(&log_lock);
}

static long guest_writev(int fd, uint64_t vector, int count, int64_t offset, int positional)
{
	struct iovec host_vector[64];
	const struct guest_iovec *guest_vector = GUEST(const struct guest_iovec *, vector);
	int index;

	if (count < 0 || count > 64)
		return -EINVAL;
	for (index = 0; index < count; index++)
	{
		host_vector[index].iov_base = GUEST(void *, guest_vector[index].base);
		host_vector[index].iov_len = guest_vector[index].length;
	}
	if (fd == 1 || fd == 2)
	{
		long total = 0;

		for (index = 0; index < count; index++)
		{
			log_bytes(fd, host_vector[index].iov_base, host_vector[index].iov_len);
			total += (long)host_vector[index].iov_len;
		}
		return total;
	}
	if (positional)
		return result_of(pwritev64(fd, host_vector, count, offset));
	return result_of(writev(fd, host_vector, count));
}

static long guest_readv(int fd, uint64_t vector, int count, int64_t offset, int positional)
{
	struct iovec host_vector[64];
	const struct guest_iovec *guest_vector = GUEST(const struct guest_iovec *, vector);
	int index;

	if (count < 0 || count > 64)
		return -EINVAL;
	for (index = 0; index < count; index++)
	{
		host_vector[index].iov_base = GUEST(void *, guest_vector[index].base);
		host_vector[index].iov_len = guest_vector[index].length;
	}
	if (positional)
		return result_of(preadv64(fd, host_vector, count, offset));
	return result_of(readv(fd, host_vector, count));
}

/* ---------- time */

static long guest_futex(uint64_t address, int operation, uint32_t value, uint64_t timeout,
	uint64_t address2, uint32_t value3)
{
	struct timespec host_timeout;
	struct timespec *timeout_pointer = NULL;
	int command = operation & FUTEX_CMD_MASK;

	/* for these operations the fourth argument is a timeout; for the
	others it is a count passed as a pointer-sized integer */
	if (command == FUTEX_WAIT || command == FUTEX_WAIT_BITSET || command == FUTEX_LOCK_PI ||
		command == FUTEX_WAIT_REQUEUE_PI)
	{
		if (timespec_in(timeout, &host_timeout))
			timeout_pointer = &host_timeout;
		return result_of(syscall(SYS_futex, GUEST(void *, address), operation, value, timeout_pointer,
			GUEST(void *, address2), value3));
	}
	return result_of(syscall(SYS_futex, GUEST(void *, address), operation, value, (void *)(uintptr_t)timeout,
		GUEST(void *, address2), value3));
}

#ifdef __arm__
/* ---------- 32-bit ARM: the calls the 64-bit app passes through */

/* the AArch64 kernel's struct stat (the guest's arch/arm64_32/kstat.h) */
struct guest_kstat
{
	uint64_t dev;
	uint64_t ino;
	uint32_t mode;
	uint32_t nlink;
	uint32_t uid;
	uint32_t gid;
	uint64_t rdev;
	uint64_t pad;
	int64_t size;
	int32_t blksize;
	int32_t pad2;
	int64_t blocks;
	int64_t atime_seconds, atime_nanoseconds;
	int64_t mtime_seconds, mtime_nanoseconds;
	int64_t ctime_seconds, ctime_nanoseconds;
	uint32_t unused[2];
};

static long stat_out(int result, const struct stat64 *value, uint64_t address)
{
	struct guest_kstat *out = GUEST(struct guest_kstat *, address);

	if (result != 0)
		return -errno;
	memset(out, 0, sizeof(*out));
	out->dev = value->st_dev;
	out->ino = value->st_ino;
	out->mode = value->st_mode;
	out->nlink = value->st_nlink;
	out->uid = value->st_uid;
	out->gid = value->st_gid;
	out->rdev = value->st_rdev;
	out->size = value->st_size;
	out->blksize = (int32_t)value->st_blksize;
	out->blocks = value->st_blocks;
	out->atime_seconds = value->st_atim.tv_sec;
	out->atime_nanoseconds = value->st_atim.tv_nsec;
	out->mtime_seconds = value->st_mtim.tv_sec;
	out->mtime_nanoseconds = value->st_mtim.tv_nsec;
	out->ctime_seconds = value->st_ctim.tv_sec;
	out->ctime_nanoseconds = value->st_ctim.tv_nsec;
	return 0;
}

/* AArch64's fcntl commands for record locks (whose struct flock, with
64-bit offsets, is 32-bit ARM's struct flock64) */
#define GUEST_F_GETLK 5
#define GUEST_F_SETLK 6
#define GUEST_F_SETLKW 7

static long arm_passthrough(long long number, long long a, long long b, long long c,
	long long d, long long e, long long f)
{
	(void)e;
	(void)f;
	switch (number)
	{
	case GUEST_SYS_read:
		return result_of(read((int)a, GUEST(void *, b), (size_t)(uint32_t)c));
	case GUEST_SYS_openat:
		return result_of(openat((int)a, GUEST(const char *, b), (int)c, (mode_t)d));
	case GUEST_SYS_close:
		return result_of(close((int)a));
	case GUEST_SYS_lseek:
	{
		off64_t result = lseek64((int)a, (off64_t)b, (int)c);

		return result == -1 ? -errno : (long)result;
	}
	case GUEST_SYS_pread64:
		return result_of(pread64((int)a, GUEST(void *, b), (size_t)(uint32_t)c, (off64_t)d));
	case GUEST_SYS_pwrite64:
		return result_of(pwrite64((int)a, GUEST(const void *, b), (size_t)(uint32_t)c, (off64_t)d));
	case GUEST_SYS_getdents64:
		/* struct linux_dirent64 is the same on both */
		return result_of(syscall(SYS_getdents64, (int)a, GUEST(void *, b), (unsigned int)c));
	case GUEST_SYS_unlinkat:
		return result_of(unlinkat((int)a, GUEST(const char *, b), (int)c));
	case GUEST_SYS_renameat:
		return result_of(renameat((int)a, GUEST(const char *, b), (int)c, GUEST(const char *, d)));
	case GUEST_SYS_renameat2:
		return result_of(syscall(SYS_renameat2, (int)a, GUEST(const char *, b), (int)c, GUEST(const char *, d),
			(unsigned int)e));
	case GUEST_SYS_mkdirat:
		return result_of(mkdirat((int)a, GUEST(const char *, b), (mode_t)c));
	case GUEST_SYS_fchmod:
		return result_of(fchmod((int)a, (mode_t)b));
	case GUEST_SYS_fchmodat:
		return result_of(syscall(SYS_fchmodat, (int)a, GUEST(const char *, b), (mode_t)c));
	case GUEST_SYS_ftruncate:
		return result_of(ftruncate64((int)a, (off64_t)b));
	case GUEST_SYS_fsync:
		return result_of(fsync((int)a));
	case GUEST_SYS_fdatasync:
		return result_of(fdatasync((int)a));
	case GUEST_SYS_fcntl:
	{
		int command = (int)b;

		if (command == GUEST_F_GETLK)
			command = F_GETLK64;
		else if (command == GUEST_F_SETLK)
			command = F_SETLK64;
		else if (command == GUEST_F_SETLKW)
			command = F_SETLKW64;
		return result_of(syscall(SYS_fcntl64, (int)a, command, (unsigned long)(uint32_t)c));
	}
	case GUEST_SYS_getcwd:
		/* the kernel's: the length of the path, with its NUL */
		return result_of(syscall(SYS_getcwd, GUEST(char *, a), (size_t)(uint32_t)b));
	case GUEST_SYS_chdir:
		return result_of(chdir(GUEST(const char *, a)));
	case GUEST_SYS_readlinkat:
		return result_of(readlinkat((int)a, GUEST(const char *, b), GUEST(char *, c), (size_t)(uint32_t)d));
	case GUEST_SYS_faccessat:
		return result_of(syscall(SYS_faccessat, (int)a, GUEST(const char *, b), (int)c));
	case GUEST_SYS_dup:
		return result_of(dup((int)a));
	case GUEST_SYS_dup3:
		return result_of(dup3((int)a, (int)b, (int)c));
	case GUEST_SYS_pipe2:
		return result_of(pipe2(GUEST(int *, a), (int)b));
	case GUEST_SYS_fstat:
	{
		struct stat64 value;

		return stat_out(fstat64((int)a, &value), &value, (uint64_t)b);
	}
	case GUEST_SYS_newfstatat:
	{
		struct stat64 value;

		return stat_out(fstatat64((int)a, GUEST(const char *, b), &value, (int)d), &value, (uint64_t)c);
	}
	case GUEST_SYS_getpid:
		return getpid();
	case GUEST_SYS_getppid:
		return getppid();
	case GUEST_SYS_gettid:
		return gettid();
	case GUEST_SYS_getuid:
		return getuid();
	case GUEST_SYS_geteuid:
		return geteuid();
	case GUEST_SYS_getgid:
		return getgid();
	case GUEST_SYS_getegid:
		return getegid();
	case GUEST_SYS_sched_yield:
		return result_of(sched_yield());
	case GUEST_SYS_getrandom:
		return result_of(syscall(SYS_getrandom, GUEST(void *, a), (size_t)(uint32_t)b, (unsigned int)c));
	case GUEST_SYS_kill:
		return result_of(kill((pid_t)a, (int)b));
	case GUEST_SYS_tkill:
		return result_of(syscall(SYS_tkill, (int)a, (int)b));
	case GUEST_SYS_tgkill:
		return result_of(syscall(SYS_tgkill, (int)a, (int)b, (int)c));
	case GUEST_SYS_rt_sigprocmask:
		/* (8-byte signal sets on both) */
		return result_of(syscall(SYS_rt_sigprocmask, (int)a, GUEST(const void *, b), GUEST(void *, c),
			(size_t)(uint32_t)d));
	case GUEST_SYS_uname:
		return result_of(uname(GUEST(struct utsname *, a)));
	case GUEST_SYS_prlimit64:
		/* (struct rlimit64 on both) */
		return result_of(syscall(SYS_prlimit64, (pid_t)a, (int)b, GUEST(const void *, c), GUEST(void *, d)));
	case GUEST_SYS_getrlimit:
		/* musl asks prlimit64 first */
		return -ENOSYS;
	case GUEST_SYS_umask:
		return umask((mode_t)a);
	case GUEST_SYS_flock:
		return result_of(flock((int)a, (int)b));
	case GUEST_SYS_membarrier:
		return result_of(syscall(SYS_membarrier, (int)a, (unsigned int)b, (int)c));
	default:
		host_logf(HOST_LOG_WARN, "guest system call %lld is not supported", number);
		return -ENOSYS;
	}
}
#endif

/* ---------- dispatch */

long long host_syscall(long long number, long long a, long long b, long long c,
	long long d, long long e, long long f)
{
	switch (number)
	{
	case GUEST_SYS_write:
		if (a == 1 || a == 2)
		{
			log_bytes((int)a, GUEST(const char *, b), (size_t)(uint32_t)c);
			return (uint32_t)c;
		}
		return result_of(write((int)a, GUEST(const void *, b), (size_t)(uint32_t)c));
	case GUEST_SYS_writev:
		return guest_writev((int)a, (uint64_t)b, (int)c, 0, 0);
	case GUEST_SYS_pwritev:
		return guest_writev((int)a, (uint64_t)b, (int)c, d, 1);
	case GUEST_SYS_readv:
		return guest_readv((int)a, (uint64_t)b, (int)c, 0, 0);
	case GUEST_SYS_preadv:
		return guest_readv((int)a, (uint64_t)b, (int)c, d, 1);

	case GUEST_SYS_clock_gettime:
	case GUEST_SYS_clock_getres:
	{
		struct timespec value;
		long result = number == GUEST_SYS_clock_gettime ? clock_gettime((clockid_t)a, &value) :
			clock_getres((clockid_t)a, &value);

		if (result == 0)
			timespec_out((uint64_t)b, &value);
		return result_of(result);
	}
	case GUEST_SYS_gettimeofday:
	{
		struct timespec value;
		struct guest_timespec *result = GUEST(struct guest_timespec *, a);

		clock_gettime(CLOCK_REALTIME, &value);
		if (result)
		{
			result->seconds = (int32_t)value.tv_sec;
			result->nanoseconds = (int32_t)(value.tv_nsec / 1000);
		}
		return 0;
	}
	case GUEST_SYS_nanosleep:
	{
		struct timespec request, remaining;
		long result;

		if (!timespec_in((uint64_t)a, &request))
			return -EFAULT;
		result = result_of(nanosleep(&request, &remaining));
		if (result == -EINTR)
			timespec_out((uint64_t)b, &remaining);
		return result;
	}
	case GUEST_SYS_clock_nanosleep:
	{
		struct timespec request, remaining;
		int result;

		if (!timespec_in((uint64_t)c, &request))
			return -EFAULT;
		result = clock_nanosleep((clockid_t)a, (int)b, &request, &remaining);
		if (result == EINTR)
			timespec_out((uint64_t)d, &remaining);
		return -result;
	}
	case GUEST_SYS_futex:
		return guest_futex((uint64_t)a, (int)b, (uint32_t)c, (uint64_t)d, (uint64_t)e, (uint32_t)f);
	case GUEST_SYS_ppoll:
	{
		struct timespec timeout;
		int has_timeout = timespec_in((uint64_t)c, &timeout);

		return result_of(ppoll(GUEST(struct pollfd *, a), (nfds_t)(uint32_t)b, has_timeout ? &timeout : NULL, NULL));
	}
	case GUEST_SYS_utimensat:
	{
		struct timespec times[2];
		const struct guest_timespec *guest_times = GUEST(const struct guest_timespec *, c);

		if (guest_times)
		{
			timespec_in((uint64_t)c, &times[0]);
			timespec_in((uint64_t)c + sizeof(struct guest_timespec), &times[1]);
		}
		return result_of(utimensat((int)a, GUEST(const char *, b), guest_times ? times : NULL, (int)d));
	}

	case GUEST_SYS_mmap:
		return host_guest_mmap((uint64_t)a, (uint64_t)b, (int)c, (int)d, (int)e, f);
	case GUEST_SYS_munmap:
		return host_guest_munmap((uint64_t)a, (uint64_t)b);
	case GUEST_SYS_mprotect:
		return host_guest_mprotect((uint64_t)a, (uint64_t)b, (int)c);
	case GUEST_SYS_madvise:
		if ((uint64_t)a + (uint64_t)b > 0x100000000ULL)
			return -EINVAL;
		return result_of(madvise(GUEST(void *, a), (size_t)(uint64_t)b, (int)c));
	case GUEST_SYS_mremap:
	case GUEST_SYS_brk:
		/* musl then falls back to mmap and copying */
		return -ENOMEM;

	case GUEST_SYS_exit:
	case GUEST_SYS_exit_group:
		host_exit((int)a);

	case GUEST_SYS_set_tid_address:
		return gettid();
	case GUEST_SYS_rt_sigaction:
	case GUEST_SYS_sigaltstack:
		/* the host owns signal handling */
		return 0;
	case GUEST_SYS_ioctl:
		return -ENOTTY;
	case GUEST_SYS_statx:
	case GUEST_SYS_statfs:
	case GUEST_SYS_fstatfs:
	case GUEST_SYS_clone:
	case GUEST_SYS_clone3:
	case GUEST_SYS_execve:
	case GUEST_SYS_rt_sigtimedwait:
	case GUEST_SYS_pselect6:
	case GUEST_SYS_epoll_pwait:
	case GUEST_SYS_sysinfo:
	case GUEST_SYS_sendmsg:
	case GUEST_SYS_recvmsg:
	case GUEST_SYS_timer_create:
	case GUEST_SYS_timer_settime:
	case GUEST_SYS_timer_gettime:
	case GUEST_SYS_setitimer:
	case GUEST_SYS_getitimer:
	case GUEST_SYS_times:
	case GUEST_SYS_getrusage:
	case GUEST_SYS_wait4:
	case GUEST_SYS_waitid:
		return -ENOSYS;

	case GUEST_SYS_read:
	case GUEST_SYS_openat:
	case GUEST_SYS_close:
	case GUEST_SYS_lseek:
	case GUEST_SYS_pread64:
	case GUEST_SYS_pwrite64:
	case GUEST_SYS_getdents64:
	case GUEST_SYS_unlinkat:
	case GUEST_SYS_renameat:
	case GUEST_SYS_renameat2:
	case GUEST_SYS_mkdirat:
	case GUEST_SYS_fchmod:
	case GUEST_SYS_fchmodat:
	case GUEST_SYS_ftruncate:
	case GUEST_SYS_fsync:
	case GUEST_SYS_fdatasync:
	case GUEST_SYS_fcntl:
	case GUEST_SYS_getcwd:
	case GUEST_SYS_chdir:
	case GUEST_SYS_readlinkat:
	case GUEST_SYS_faccessat:
	case GUEST_SYS_dup:
	case GUEST_SYS_dup3:
	case GUEST_SYS_pipe2:
	case GUEST_SYS_fstat:
	case GUEST_SYS_newfstatat:
	case GUEST_SYS_getpid:
	case GUEST_SYS_getppid:
	case GUEST_SYS_gettid:
	case GUEST_SYS_getuid:
	case GUEST_SYS_geteuid:
	case GUEST_SYS_getgid:
	case GUEST_SYS_getegid:
	case GUEST_SYS_sched_yield:
	case GUEST_SYS_getrandom:
	case GUEST_SYS_kill:
	case GUEST_SYS_tkill:
	case GUEST_SYS_tgkill:
	case GUEST_SYS_rt_sigprocmask:
	case GUEST_SYS_uname:
	case GUEST_SYS_prlimit64:
	case GUEST_SYS_getrlimit:
	case GUEST_SYS_umask:
	case GUEST_SYS_flock:
	case GUEST_SYS_membarrier:
#ifdef __arm__
		return arm_passthrough(number, a, b, c, d, e, f);
#else
		return result_of(syscall(number, a, b, c, d, e, f));
#endif

	default:
		host_logf(HOST_LOG_WARN, "guest system call %lld is not supported", number);
		return -ENOSYS;
	}
}
