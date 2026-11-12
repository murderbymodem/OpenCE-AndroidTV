/*
GUEST_GL_PROFILE.C

debug.gl_profile for the 32-bit ARM guest: the time each OpenGL ES function
takes in the driver, to find what a frame's time on the CPU goes to where
a profiler cannot name the driver's functions (the Mali driver's have no
symbols). The generated wrappers (guest_gl.c, tools/android_gl_stubs.py
--arch arm) note each call; the app's own GL helpers (host_gl.c) and the
buffer swap are timed here. Every 120 frames the log has the functions that
took the most, with their calls and time each frame.
*/

#ifdef HALO_ARM32_GUEST

#include "guest_host.h"

#include <stdio.h>
#include <string.h>

extern const unsigned int guest_gl_profile_count;
extern const char *const guest_gl_profile_names[];

#define EXTRA_NAMES 5
#define MAXIMUM_ENTRIES 512
#define REPORT_FRAMES 120
#define REPORT_LINES 14

enum
{
	_extra_read_buffer,
	_extra_buffer_write,
	_extra_fence_frame,
	_extra_wait_frame,
	_extra_swap
};

static const char *const extra_names[EXTRA_NAMES] =
{
	"(host_gl_read_buffer)", "(host_gl_buffer_write)", "(host_gl_fence_frame)", "(host_gl_wait_frame)",
	"(swap buffers)"
};

int guest_gl_profile_on;
static unsigned long long calls[MAXIMUM_ENTRIES];
static unsigned long long nanoseconds[MAXIMUM_ENTRIES];
static unsigned long frames;

static void note(unsigned int index, long long start)
{
	long long elapsed = host_gl_clock() - start;

	if (index >= MAXIMUM_ENTRIES)
		return;
	calls[index]++;
	nanoseconds[index] += (unsigned long long)(elapsed > 0 ? elapsed : 0);
}

void guest_gl_profile_note(unsigned int index, long long start)
{
	note(index, start);
}

static unsigned int extra_index(unsigned int extra)
{
	return guest_gl_profile_count + extra;
}

void guest_gl_profiled_read_buffer(unsigned int buffer, unsigned int offset, unsigned int size, void *data)
{
	long long start = guest_gl_profile_on ? host_gl_clock() : 0;

	host_gl_read_buffer(buffer, offset, size, data);
	if (guest_gl_profile_on)
		note(extra_index(_extra_read_buffer), start);
}

void guest_gl_profiled_buffer_write(unsigned int target, unsigned int offset, unsigned int size, const void *data)
{
	long long start = guest_gl_profile_on ? host_gl_clock() : 0;

	host_gl_buffer_write(target, offset, size, data);
	if (guest_gl_profile_on)
		note(extra_index(_extra_buffer_write), start);
}

void guest_gl_profiled_fence_frame(unsigned int slot)
{
	long long start = guest_gl_profile_on ? host_gl_clock() : 0;

	host_gl_fence_frame(slot);
	if (guest_gl_profile_on)
		note(extra_index(_extra_fence_frame), start);
}

void guest_gl_profiled_wait_frame(unsigned int slot)
{
	long long start = guest_gl_profile_on ? host_gl_clock() : 0;

	host_gl_wait_frame(slot);
	if (guest_gl_profile_on)
		note(extra_index(_extra_wait_frame), start);
}

/* the buffer swap (guest_sdl.c) */
void guest_gl_profile_swap(long long start)
{
	if (guest_gl_profile_on)
		note(extra_index(_extra_swap), start);
}

void guest_gl_profile_frame(void)
{
	unsigned int count = guest_gl_profile_count + EXTRA_NAMES, index, line;
	unsigned long long total = 0;
	char text[160];

	if (!guest_gl_profile_on || ++frames < REPORT_FRAMES)
		return;
	if (count > MAXIMUM_ENTRIES)
		count = MAXIMUM_ENTRIES;
	for (index = 0; index < count; index++)
		total += nanoseconds[index];
	snprintf(text, sizeof(text), "gl profile: %lu frames, %.2f ms a frame in GL (the swap included), the most:",
		frames, (double)total / 1e6 / (double)frames);
	host_log(5, text);
	for (line = 0; line < REPORT_LINES; line++)
	{
		unsigned int best = count;
		const char *name;

		for (index = 0; index < count; index++)
		{
			if (nanoseconds[index] && (best == count || nanoseconds[index] > nanoseconds[best]))
				best = index;
		}
		if (best == count)
			break;
		name = best < guest_gl_profile_count ? guest_gl_profile_names[best] :
			extra_names[best - guest_gl_profile_count];
		snprintf(text, sizeof(text), "gl profile:   %-34s %7.1f calls %7.3f ms a frame (%.1f us each)", name,
			(double)calls[best] / (double)frames, (double)nanoseconds[best] / 1e6 / (double)frames,
			(double)nanoseconds[best] / 1e3 / (double)calls[best]);
		host_log(5, text);
		nanoseconds[best] = 0;
	}
	memset(calls, 0, sizeof(calls));
	memset(nanoseconds, 0, sizeof(nanoseconds));
	frames = 0;
}

#endif
