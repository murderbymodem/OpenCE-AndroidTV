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

/* A sample is taken in a signal handler, on the thread it is of, which may
hold any lock at that moment: the log's, the allocator's, the guest
memory's. So the handler takes none and calls nothing that might: it copies
the registers and the frame chain into a slot of a ring, and the sampler's
own thread writes the slots to the log afterwards. (Logging from the
handler was safe enough once a second; fifty times a second it soon
interrupted a thread inside a lock it then waited for, and the app froze.) */
#define SAMPLE_SLOTS 256
#define SAMPLE_FRAMES 12
enum
{
	_sample_slot_empty,
	_sample_slot_writing,
	_sample_slot_ready,
};

static struct sample_slot
{
	int state;
	pid_t thread;
	uint32_t pc, lr;
	int frame_count;
	uint32_t frames[SAMPLE_FRAMES];
} sample_slots[SAMPLE_SLOTS];
static unsigned int sample_next;

static void sample_handler(int signal_number, siginfo_t *information, void *context)
{
	const ucontext_t *ucontext = context;
	const struct sigcontext *registers = (const struct sigcontext *)&ucontext->uc_mcontext;
	struct sample_slot *slot = &sample_slots[__atomic_fetch_add(&sample_next, 1, __ATOMIC_RELAXED) % SAMPLE_SLOTS];
	uint64_t fp = HOST_CONTEXT_FP(registers);
	int depth, empty = _sample_slot_empty;

	(void)signal_number;
	(void)information;
	/* (a slot the log has not taken yet: this sample is dropped) */
	if (!__atomic_compare_exchange_n(&slot->state, &empty, _sample_slot_writing, 0, __ATOMIC_ACQUIRE, __ATOMIC_RELAXED))
		return;
	slot->thread = (pid_t)syscall(SYS_gettid);
	slot->pc = (uint32_t)HOST_CONTEXT_PC(registers);
	slot->lr = (uint32_t)HOST_CONTEXT_LR(registers);
	/* (the frames while each is in the guest's memory, which is looked up
	only if its lock is free: otherwise the sample has no callers) */
	for (depth = 0; depth < SAMPLE_FRAMES && fp && fp < 0x100000000ULL && !(fp & (sizeof(host_frame_word) - 1)) &&
		host_low_owns_try(fp, 2 * sizeof(host_frame_word)) == 1; depth++)
	{
		const host_frame_word *frame = (const host_frame_word *)(uintptr_t)fp;

		slot->frames[depth] = (uint32_t)frame[1];
		if (frame[0] <= fp)
		{
			depth++;
			break;
		}
		fp = frame[0];
	}
	slot->frame_count = depth;
	__atomic_store_n(&slot->state, _sample_slot_ready, __ATOMIC_RELEASE);
}

/* the ready samples, written to the log by the sampler's thread. A thread
that is where it was at its last sample (waiting, mostly) is not written
again: "same N" counts those, when it has moved on or every fifty. */
static void samples_log(void)
{
	static struct
	{
		pid_t thread;
		uint32_t pc, lr, frame;
		unsigned int same;
	} last[MAXIMUM_THREADS];
	static unsigned int taken;
	unsigned int end = __atomic_load_n(&sample_next, __ATOMIC_RELAXED);

	/* (more than a ring's worth since: the oldest were dropped by their
	handlers, or are still being written) */
	if (end - taken > SAMPLE_SLOTS)
		taken = end - SAMPLE_SLOTS;
	for (; taken != end; taken++)
	{
		struct sample_slot *slot = &sample_slots[taken % SAMPLE_SLOTS];
		char line[512];
		int length, depth, index, entry = -1;

		if (__atomic_load_n(&slot->state, __ATOMIC_ACQUIRE) != _sample_slot_ready)
		{
			/* (not written yet: next time, unless it never was) */
			if (__atomic_load_n(&slot->state, __ATOMIC_ACQUIRE) == _sample_slot_writing)
				break;
			continue;
		}
		for (index = 0; index < MAXIMUM_THREADS; index++)
		{
			if (last[index].thread == slot->thread || (entry < 0 && !last[index].thread))
				entry = index;
			if (last[index].thread == slot->thread)
				break;
		}
		if (entry >= 0 && last[entry].thread == slot->thread && last[entry].pc == slot->pc &&
			last[entry].lr == slot->lr && last[entry].frame == (slot->frame_count ? slot->frames[0] : 0))
		{
			if (++last[entry].same == 50)
			{
				host_logf(HOST_LOG_INFO, "sample tid %d: same %u", (int)slot->thread, last[entry].same);
				last[entry].same = 0;
			}
		}
		else
		{
			if (entry >= 0)
			{
				if (last[entry].thread == slot->thread && last[entry].same)
					host_logf(HOST_LOG_INFO, "sample tid %d: same %u", (int)slot->thread, last[entry].same);
				last[entry].thread = slot->thread;
				last[entry].pc = slot->pc;
				last[entry].lr = slot->lr;
				last[entry].frame = slot->frame_count ? slot->frames[0] : 0;
				last[entry].same = 0;
			}
			length = snprintf(line, sizeof(line), "sample tid %d: pc %x lr %x", (int)slot->thread, slot->pc, slot->lr);
			for (depth = 0; depth < slot->frame_count && length < (int)sizeof(line) - 12; depth++)
				length += snprintf(line + length, sizeof(line) - length, " %x", slot->frames[depth]);
			host_logf(HOST_LOG_INFO, "%s", line);
		}
		__atomic_store_n(&slot->state, _sample_slot_empty, __ATOMIC_RELEASE);
	}
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
		/* (the last round's samples, their handlers long done) */
		samples_log();
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
