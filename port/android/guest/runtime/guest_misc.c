/*
GUEST_MISC.C

Runtime support functions the guest's code generator (clang's Darwin
arm64_32 target) or the platform layer expect, which neither the game nor
musl provides.
*/

#include "guest_host.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

/* Darwin's combined sine and cosine, which clang substitutes for
sin()/cos() pairs of the same argument */
struct guest_sincos_result { double sine, cosine; };
struct guest_sincosf_result { float sine, cosine; };

__attribute__((no_builtin)) struct guest_sincos_result __sincos_stret(double x)
{
	struct guest_sincos_result result;

	/* no_builtin: clang would turn sincos(), like a sin() and cos() pair,
	back into a call to this function */
	sincos(x, &result.sine, &result.cosine);
	return result;
}

__attribute__((no_builtin)) struct guest_sincosf_result __sincosf_stret(float x)
{
	struct guest_sincosf_result result;

	sincosf(x, &result.sine, &result.cosine);
	return result;
}

/* Darwin's exp10, which clang substitutes for pow(10, x) (so these must
not be written with pow, which would become calls to themselves) */
__attribute__((no_builtin)) float __exp10f(float x)
{
	return exp10f(x);
}

__attribute__((no_builtin)) double __exp10(double x)
{
	return exp10(x);
}

__attribute__((no_builtin)) void __bzero(void *buffer, size_t size)
{
	memset(buffer, 0, size);
}

/* no_builtin: the loop must not become a call to itself */
__attribute__((no_builtin)) void memset_pattern4(void *buffer, const void *pattern, size_t size)
{
	unsigned char *out = buffer;
	size_t index;

	for (index = 0; index < size; index++)
		out[index] = ((const unsigned char *)pattern)[index & 3];
}

/* no_builtin: the loop must not become a call to itself */
__attribute__((no_builtin)) void memset_pattern8(void *buffer, const void *pattern, size_t size)
{
	unsigned char *out = buffer;
	size_t index;

	for (index = 0; index < size; index++)
		out[index] = ((const unsigned char *)pattern)[index & 7];
}

/* no_builtin: the loop must not become a call to itself */
__attribute__((no_builtin)) void memset_pattern16(void *buffer, const void *pattern, size_t size)
{
	unsigned char *out = buffer;
	size_t index;

	for (index = 0; index < size; index++)
		out[index] = ((const unsigned char *)pattern)[index & 15];
}

void __stack_chk_fail(void)
{
	host_abort("stack smashing detected");
}

/* <execinfo.h>: the guest has no unwinder */
int backtrace(void **frames, int size)
{
	(void)frames;
	(void)size;
	return 0;
}

void backtrace_symbols_fd(void *const *frames, int size, int fd)
{
	(void)frames;
	(void)size;
	(void)fd;
}

#ifdef HALO_ARM32_GUEST
/* MSVC's compiler barrier (source/scenario/scenario.c), which clang does
not take as a builtin for this target: the call, to a function it cannot
see into, is the barrier */
__attribute__((noinline)) void _ReadWriteBarrier(void)
{
	__asm__ __volatile__("" ::: "memory");
}
#endif

/* ---------- diagnostic builds' shift reports

A build configured with --android-guest-cflags=-fsanitize=shift-exponent
(configure.py) calls this at every shift by as many bits as its type has,
or more, or by a negative count. x86 and AArch64 count a 32-bit shift
modulo 32, which the game may have relied on; 32-bit ARM counts it modulo
256, so such a shift gives that guest other results (0 rather than the
value shifted by count % 32). Each place is logged once, with the count, to
logcat (tag halo); the shift goes on as the processor makes it. */

struct ubsan_location
{
	const char *file;
	unsigned int line;
	unsigned int column;
};

struct ubsan_type
{
	unsigned short kind;
	/* for integers: log2 of the width in bits, shifted left once, and 1 if
	signed */
	unsigned short info;
	char name[1];
};

struct ubsan_shift_data
{
	struct ubsan_location location;
	const struct ubsan_type *left_type;
	const struct ubsan_type *right_type;
};

#define UBSAN_MAXIMUM_SITES 512

static const void *ubsan_sites[UBSAN_MAXIMUM_SITES];
static unsigned int ubsan_site_count;

/* an integer operand: in the handle, or for one wider than a pointer, at
the address it holds */
static long long ubsan_value(const struct ubsan_type *type, unsigned long handle)
{
	unsigned int bits = 1u << (type->info >> 1);
	int is_signed = type->info & 1;

	if (bits > 32)
		return *(const long long *)handle;
	if (is_signed)
		return bits == 32 ? (long long)(int)handle : (long long)(long)handle;
	return (long long)(unsigned long)handle;
}

void __ubsan_handle_shift_out_of_bounds(struct ubsan_shift_data *data, unsigned long left, unsigned long right)
{
	char line[256];
	unsigned int index, count = __atomic_load_n(&ubsan_site_count, __ATOMIC_ACQUIRE);

	for (index = 0; index < count && index < UBSAN_MAXIMUM_SITES; index++)
	{
		if (ubsan_sites[index] == data)
			return;
	}
	index = __atomic_fetch_add(&ubsan_site_count, 1, __ATOMIC_ACQ_REL);
	if (index >= UBSAN_MAXIMUM_SITES)
		return;
	ubsan_sites[index] = data;
	snprintf(line, sizeof(line), "shift out of range at %s:%u:%u: %s %lld by %s %lld",
		data->location.file ? data->location.file : "?", data->location.line, data->location.column,
		data->left_type->name, ubsan_value(data->left_type, left), data->right_type->name,
		ubsan_value(data->right_type, right));
	host_log(5 /* ANDROID_LOG_WARN */, line);
}

void __ubsan_handle_shift_out_of_bounds_abort(struct ubsan_shift_data *data, unsigned long left, unsigned long right)
{
	__ubsan_handle_shift_out_of_bounds(data, left, right);
}
