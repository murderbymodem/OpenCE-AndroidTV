/*
HOST_DEBUG.C

A sampler for finding where the guest spends its time (or hangs) on a
device without root, where debuggerd cannot attach: with sample_seconds in
config.toml's [debug], every guest thread is interrupted that often and its program
counter, link register and frame chain are written to logcat. The addresses
symbolize against build/android/halo_guest.elf (llvm-symbolizer
--obj=build/android/halo_guest.elf 0x...).
*/

#include "host.h"

#include <pthread.h>
#include <signal.h>
#include <stdlib.h>
#include <string.h>
#include <sys/syscall.h>
#include <time.h>
#include <ucontext.h>
#include <unistd.h>

#define MAXIMUM_THREADS 64
#define SAMPLE_SIGNAL SIGURG

static pid_t guest_threads[MAXIMUM_THREADS];
static pthread_mutex_t threads_lock = PTHREAD_MUTEX_INITIALIZER;

void host_debug_thread_started(void)
{
	int index;

	pthread_mutex_lock(&threads_lock);
	for (index = 0; index < MAXIMUM_THREADS; index++)
	{
		if (!guest_threads[index])
		{
			guest_threads[index] = gettid();
			break;
		}
	}
	pthread_mutex_unlock(&threads_lock);
}

void host_debug_thread_exited(void)
{
	pid_t self = gettid();
	int index;

	pthread_mutex_lock(&threads_lock);
	for (index = 0; index < MAXIMUM_THREADS; index++)
	{
		if (guest_threads[index] == self)
			guest_threads[index] = 0;
	}
	pthread_mutex_unlock(&threads_lock);
}

static void sample_handler(int signal_number, siginfo_t *information, void *context)
{
	const ucontext_t *ucontext = context;
	const struct sigcontext *registers = (const struct sigcontext *)&ucontext->uc_mcontext;
	char line[512];
	int length, depth;
	uint64_t fp = HOST_CONTEXT_FP(registers);

	(void)signal_number;
	(void)information;
	length = snprintf(line, sizeof(line), "sample tid %d: pc %llx lr %llx", gettid(),
		(unsigned long long)HOST_CONTEXT_PC(registers), (unsigned long long)HOST_CONTEXT_LR(registers));
	for (depth = 0; depth < 12 && fp && fp < 0x100000000ULL && !(fp & (sizeof(host_frame_word) - 1)) &&
		host_low_owns(fp, 2 * sizeof(host_frame_word)); depth++)
	{
		const host_frame_word *frame = (const host_frame_word *)(uintptr_t)fp;

		length += snprintf(line + length, sizeof(line) - length, " %llx", (unsigned long long)frame[1]);
		if (frame[0] <= fp)
			break;
		fp = frame[0];
	}
	host_logf(HOST_LOG_INFO, "%s", line);
}

/* (the time between samples: a fraction of a second for a profile with its
callers, which a profiler cannot give of the 32-bit guest, whole seconds for
finding where a thread hangs) */
static void *sampler(void *context)
{
	unsigned int milliseconds = (unsigned int)(uintptr_t)context;

	for (;;)
	{
		pid_t threads[MAXIMUM_THREADS];
		struct timespec interval = { milliseconds / 1000, (long)(milliseconds % 1000) * 1000000L };
		int index;

		nanosleep(&interval, NULL);
		pthread_mutex_lock(&threads_lock);
		memcpy(threads, guest_threads, sizeof(threads));
		pthread_mutex_unlock(&threads_lock);
		for (index = 0; index < MAXIMUM_THREADS; index++)
		{
			if (threads[index])
				syscall(SYS_tgkill, getpid(), threads[index], SAMPLE_SIGNAL);
		}
	}
	return NULL;
}

void host_debug_start_sampler(const char *setting)
{
	struct sigaction action;
	pthread_t thread;
	double seconds = setting ? atof(setting) : 0.0;
	unsigned int milliseconds;

	if (!(seconds > 0.0))
		return;
	/* (a hundred samples a second at most: each is a line in the log) */
	milliseconds = seconds < 0.01 ? 10 : seconds > 3600.0 ? 3600000 : (unsigned int)(seconds * 1000.0 + 0.5);
	memset(&action, 0, sizeof(action));
	action.sa_sigaction = sample_handler;
	action.sa_flags = SA_SIGINFO | SA_RESTART;
	sigemptyset(&action.sa_mask);
	sigaction(SAMPLE_SIGNAL, &action, NULL);
	if (pthread_create(&thread, NULL, sampler, (void *)(uintptr_t)milliseconds) == 0)
		pthread_detach(thread);
	host_logf(HOST_LOG_INFO, "sampling guest threads every %u ms", milliseconds);
}
