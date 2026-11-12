/*
GUEST_STRING.C

String functions faster than musl's generic C ones, which the guest takes
in their place (tools/android_build.py MUSL_EXCLUDE). The renderer's state
caches compare their keys with memcmp on every draw (the pixel shaders'
keys, the vertex constants, the uniforms), and musl's compares a byte at a
time.
*/

#include <stddef.h>
#include <stdint.h>
#include <string.h>

/* eight bytes at a time while they match (AArch64 loads them unaligned),
then the bytes that differ, or the tail. no_builtin: clang would turn the
loops back into a call to memcmp */
__attribute__((no_builtin)) int memcmp(const void *left, const void *right, size_t size)
{
	const unsigned char *l = left, *r = right;

	for (; size >= 8; size -= 8, l += 8, r += 8)
	{
		uint64_t a, b;

		__builtin_memcpy(&a, l, 8);
		__builtin_memcpy(&b, r, 8);
		if (a != b)
			break;
	}
	for (; size; size--, l++, r++)
	{
		if (*l != *r)
			return *l - *r;
	}
	return 0;
}

#ifdef HALO_ARM32_GUEST
/* ---------- memcpy and memmove on 32-bit ARM

musl's generic C ones copy a word at a time once both pointers are aligned,
with shifts where they are not aligned alike (memcpy) or a byte at a time
(memmove), and the game copies its unaligned data all the time (the vertex
streams, the network's messages, textures on their way to the driver).
These copy sixteen bytes at a time through NEON registers, whose loads and
stores take any address (the 16 bytes as a vector of bytes: VLD1.8 and
VST1.8, never an aligned form), then what is left in eight, four, two and
one. no_builtin: clang would turn the loops back into calls to memcpy. */

typedef unsigned char guest_bytes16 __attribute__((vector_size(16), aligned(1), may_alias));
typedef uint64_t guest_bytes8 __attribute__((aligned(1), may_alias));
typedef uint32_t guest_bytes4 __attribute__((aligned(1), may_alias));
typedef uint16_t guest_bytes2 __attribute__((aligned(1), may_alias));

/* forward: each step loads all it copies before it stores, so a destination
below an overlapping source is copied correctly (memmove's case) */
__attribute__((no_builtin, always_inline)) static inline void copy_forward(unsigned char *d, const unsigned char *s,
	size_t size)
{
	for (; size >= 64; size -= 64, d += 64, s += 64)
	{
		guest_bytes16 a = *(const guest_bytes16 *)s, b = *(const guest_bytes16 *)(s + 16);
		guest_bytes16 c = *(const guest_bytes16 *)(s + 32), e = *(const guest_bytes16 *)(s + 48);

		*(guest_bytes16 *)d = a;
		*(guest_bytes16 *)(d + 16) = b;
		*(guest_bytes16 *)(d + 32) = c;
		*(guest_bytes16 *)(d + 48) = e;
	}
	for (; size >= 16; size -= 16, d += 16, s += 16)
	{
		guest_bytes16 a = *(const guest_bytes16 *)s;

		*(guest_bytes16 *)d = a;
	}
	if (size & 8)
	{
		guest_bytes8 a = *(const guest_bytes8 *)s;

		*(guest_bytes8 *)d = a;
		d += 8, s += 8;
	}
	if (size & 4)
	{
		guest_bytes4 a = *(const guest_bytes4 *)s;

		*(guest_bytes4 *)d = a;
		d += 4, s += 4;
	}
	if (size & 2)
	{
		guest_bytes2 a = *(const guest_bytes2 *)s;

		*(guest_bytes2 *)d = a;
		d += 2, s += 2;
	}
	if (size & 1)
		*d = *s;
}

/* (no restrict: the game's own copies may overlap with the destination
below the source, which the generic memcpy copied correctly, as this does) */
__attribute__((no_builtin)) void *memcpy(void *destination, const void *source, size_t size)
{
	copy_forward(destination, source, size);
	return destination;
}

__attribute__((no_builtin)) void *memmove(void *destination, const void *source, size_t size)
{
	unsigned char *d = destination;
	const unsigned char *s = source;

	/* (the destination is below the source, or past its end: forward) */
	if ((uintptr_t)d - (uintptr_t)s >= size)
	{
		copy_forward(d, s, size);
		return destination;
	}
	/* the destination is inside the source: from the end, each step again
	loading all it copies before it stores */
	while (size >= 16)
	{
		guest_bytes16 a;

		size -= 16;
		a = *(const guest_bytes16 *)(s + size);
		*(guest_bytes16 *)(d + size) = a;
	}
	if (size & 8)
	{
		guest_bytes8 a;

		size -= 8;
		a = *(const guest_bytes8 *)(s + size);
		*(guest_bytes8 *)(d + size) = a;
	}
	if (size & 4)
	{
		guest_bytes4 a;

		size -= 4;
		a = *(const guest_bytes4 *)(s + size);
		*(guest_bytes4 *)(d + size) = a;
	}
	if (size & 2)
	{
		guest_bytes2 a;

		size -= 2;
		a = *(const guest_bytes2 *)(s + size);
		*(guest_bytes2 *)(d + size) = a;
	}
	if (size & 1)
		*d = *s;
	return destination;
}
#endif
