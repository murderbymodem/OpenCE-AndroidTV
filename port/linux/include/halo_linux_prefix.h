/*
HALO_LINUX_PREFIX.H

Force-included ahead of every translation unit in the native Linux build
(clang -include). It reproduces the handful of MSVC/XDK environment
assumptions that the game source relies on, so the source itself can stay
byte-for-byte identical to what the matching MSVC build compiles.
*/

#ifndef __HALO_LINUX_PREFIX_H
#define __HALO_LINUX_PREFIX_H

#if !defined(__i386__) && !defined(HALO_ANDROID_GUEST) && !defined(__wasm32__)
#error the Linux port targets 32-bit x86: game data structures assume 32-bit pointers
#endif

/* ---------- data that may lie at any address

A typedef of a type with HALO_UNALIGNED is the same type at an alignment of
1: the compiler loads and stores it in pieces that may lie anywhere. 32-bit
ARM (the Android armeabi-v7a guest) stops a program at floating-point,
double-word and multiple loads of an address that is not a multiple of 4,
where x86 and AArch64 do not (port/android/host/host_alignment.c performs
them, at the cost of a signal each); elsewhere it is nothing. */

#ifdef HALO_ARM32_GUEST
#define HALO_UNALIGNED __attribute__((aligned(1)))
#else
#define HALO_UNALIGNED
#endif

/* ---------- rounding on ARMv7

The game rounds a real number to an integer with __builtin_rint (fast_ftol,
the colours' packing: x87's FISTP in the original), which is an instruction
on x86, AArch64 and ARMv8 but a call to rint on ARMv7, with the number moved
through core registers on the way in and out (the softfp convention). The
32-bit Android guest's ARMv7 image rounds in line instead, as that rint
does: below 2^52 a double plus and minus 2^52 is the nearest integer, ties
to even, under the default rounding mode, and takes the number's sign (so
that -0.25 rounds to -0); larger numbers are integers already, and a NaN
stays one. The result is rint's, bit for bit. */
#if defined(HALO_ARM32_GUEST) && !defined(__ARM_FEATURE_DIRECTED_ROUNDING)
static __inline__ double halo_arm32_rint(double value)
{
	double magnitude = __builtin_fabs(value);

	if (magnitude < 4503599627370496.0)
	{
		magnitude = (magnitude + 4503599627370496.0) - 4503599627370496.0;
		value = __builtin_copysign(magnitude, value);
	}
	return value;
}
#define __builtin_rint(value) halo_arm32_rint(value)
#endif

/* ---------- WebAssembly: no return addresses

WebAssembly code has no address to return to: Emscripten makes one up by
walking a JavaScript stack trace, which the game's datum lookups (data.c's
data_usable, for its report of a broken array) and the HUD's paired stack
checks (hud_draw.c's get_return_eip) asked for thousands of times a frame:
most of a frame's time. The address would name nothing a log could use
anyway, so it is 0 there. */
#ifdef __wasm32__
#define __builtin_return_address(level) ((void *)0)
#endif

/* ---------- XDK architecture selection (MSVC predefines these) */

#define _X86_ 1
#define _M_IX86 600
#define _STDCALL_SUPPORTED 1
#define _INTEGRAL_MAX_BITS 64
#define _WCHAR_T_DEFINED
#define _USE_MATH_DEFINES
/* the XDK's COM headers decorate methods with __export when _WIN32 is unset */
#define __export

/* ---------- MSVC inline semantics

MSVC gives C `__inline` functions COMDAT (pick-any) linkage. ELF C has no
equivalent, so every translation unit gets its own private copy instead.
Clang only warns about the resulting `static static`. */

#define __inline static __inline__
#define _inline static __inline__
#define __forceinline static __inline__ __attribute__((always_inline))

/* An inline function that also has an ordinary prototype keeps external
linkage; the generated halo_msvc_semantics.h marks every inline function
name `#pragma weak`, making those definitions pick-any like a COMDAT. */

/* glibc spells its own extern-inline helpers with __inline; keep it from
emitting them so the redefinition above cannot reach them. */
#define __NO_INLINE__ 1

/* ---------- __declspec(selectany) data (XDK D3DCONST tables) */

#define DECLSPEC_SELECTANY __attribute__((weak))

/* ---------- MSVC intrinsics

Clang predeclares the MSVC _Interlocked* builtins with prototypes that
conflict with the XDK's WINAPI declarations; route the XDK names to the
platform layer instead. */

#define _InterlockedCompareExchange halo_linux_InterlockedCompareExchange
#define _InterlockedDecrement halo_linux_InterlockedDecrement
#define _InterlockedExchange halo_linux_InterlockedExchange
#define _InterlockedExchangeAdd halo_linux_InterlockedExchangeAdd
#define _InterlockedIncrement halo_linux_InterlockedIncrement

/* ---------- structured exception handling

Only the top-level crash handler in main() uses SEH. POSIX has no equivalent
(the platform layer installs signal handlers instead), so the guarded block
always runs and the handler is compiled out. */

#define __try if (1)
#define __except(filter) else if (0)
#define __finally
#define __leave

/* ---------- multiplayer session limits of the native builds */

#include "halo_port_limits.h"

/* the Xbox Winsock headers' fd_set in game units (platform units see glibc's,
which is larger) */
#ifndef HALO_LINUX_PLATFORM_LAYER
#define FD_SETSIZE HALO_PORT_FD_SETSIZE
#endif

/* ---------- Winsock

Game code sees the XDK's Winsock under private names (see the header). The
platform layer includes the XDK headers itself, via platform.h. */

#ifndef HALO_LINUX_PLATFORM_LAYER
#include "halo_linux_winsock_names.h"
#include "halo_linux_source_fixups.h"
#endif

/* ---------- MSVC built-in types */

#include <stddef.h>

#endif /* __HALO_LINUX_PREFIX_H */
