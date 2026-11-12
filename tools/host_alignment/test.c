/*
Tests of port/android/host/host_alignment.c (tools/test_host_alignment.py):
the 32-bit ARM guest's unaligned accesses, decoded and performed on a
signal's saved registers. Built as a freestanding 32-bit x86 program with
the stand-in headers in include/, so it runs on the build machine; the
instructions are the ARM assembler's encodings.
*/
#include "host.h"
#include <signal.h>
#include <ucontext.h>
#include <string.h>
int host_alignment_fixup(siginfo_t *information, void *context);
struct host_guest_image host_image;
static int logs;
void host_logf(int priority, const char *format, ...) { (void)priority; (void)format; logs++; }
/* (a clock that advances a second a call, so that the reports are made) */
#include <time.h>
int clock_gettime(int clock, struct timespec *time) { static long seconds; (void)clock; time->tv_sec = ++seconds; time->tv_nsec = 0; return 0; }
void *memcpy(void *d, const void *s, size_t n) { unsigned char *a = d; const unsigned char *b = s; while (n--) *a++ = *b++; return d; }
static void sys_write(const char *s) { int n = 0; while (s[n]) n++; __asm__ volatile("int $0x80" :: "a"(4), "b"(1), "c"(s), "d"(n) : "memory"); }
static void sys_exit(int code) { __asm__ volatile("int $0x80" :: "a"(1), "b"(code)); for (;;); }
static void hex(uint32_t v) { char b[11] = "0x00000000"; int i; for (i = 0; i < 8; i++) b[9 - i] = "0123456789abcdef"[(v >> (i * 4)) & 15]; sys_write(b); }
static int failures, tests;
static void check(const char *what, uint32_t got, uint32_t want)
{
	tests++;
	if (got != want) { failures++; sys_write("FAIL "); sys_write(what); sys_write(" got "); hex(got); sys_write(" want "); hex(want); sys_write("\n"); }
}
static uint32_t code[16] __attribute__((aligned(4)));
static unsigned char memory[256] __attribute__((aligned(8)));
static ucontext_t context;
static siginfo_t info;
#define R(n) ((&context.uc_mcontext.arm_r0)[n])
static uint32_t *vfp(void) { return (uint32_t *)((unsigned char *)context.uc_regspace + 8 + 8); }  /* after a dummy block */
static unsigned char *M(int offset) { return memory + 64 + offset; }
static uint32_t A(int offset) { return (uint32_t)(uintptr_t)M(offset); }
static uint32_t word_at(int offset) { uint32_t v; memcpy(&v, M(offset), 4); return v; }
static int run(uint32_t instruction)
{
	code[0] = instruction;
	context.uc_mcontext.arm_pc = (uint32_t)(uintptr_t)&code[0];
	info.si_code = BUS_ADRALN;
	return host_alignment_fixup(&info, &context);
}
static void reset(void)
{
	int i;
	memset_loop: for (i = 0; i < (int)sizeof(context); i++) ((unsigned char *)&context)[i] = 0;
	for (i = 0; i < 256; i++) memory[i] = (unsigned char)(i * 7 + 1);
	/* a dummy block, then the VFP frame */
	context.uc_regspace[0] = 0x12345678; context.uc_regspace[1] = 8;
	context.uc_regspace[2] = 0x56465001; context.uc_regspace[3] = 0x120;
	context.uc_mcontext.arm_cpsr = 0x10; /* user, ARM state, flags clear */
}
static uint32_t PC0(void) { return (uint32_t)(uintptr_t)&code[0]; }
void _start(void)
{
	host_image.base = (uint32_t)(uintptr_t)code; host_image.end = host_image.base + sizeof(code);

	/* vldr s0, [r9] at an address 2 off: the crash */
	reset(); R(9) = A(2);
	check("vldr s0 handled", run(0xed990a00), 1);
	check("vldr s0 value", vfp()[0], word_at(2));
	check("vldr s0 pc", context.uc_mcontext.arm_pc, PC0() + 4);
	check("vldr s0 s1 untouched", vfp()[1], 0);

	/* vldr s3, [r1, #-8]: odd single register = high half of d1 */
	reset(); R(1) = A(11);
	check("vldr s3", run(0xed511a02), 1);
	check("vldr s3 value", vfp()[3], word_at(3));
	check("vldr s3 r1 kept", R(1), A(11));

	/* vstr d17, [r2, #16] */
	reset(); R(2) = A(1); vfp()[34] = 0x11223344; vfp()[35] = 0x55667788;
	check("vstr d17", run(0xedc21b04), 1);
	check("vstr d17 low", word_at(17), 0x11223344);
	check("vstr d17 high", word_at(21), 0x55667788);

	/* vldmia r3!, {d0-d2} */
	reset(); R(3) = A(6);
	check("vldmia", run(0xecb30b06), 1);
	check("vldmia d0", vfp()[0], word_at(6));
	check("vldmia d2 high", vfp()[5], word_at(26));
	check("vldmia wb", R(3), A(30));

	/* vstmdb r4!, {s2-s5} */
	reset(); R(4) = A(18); vfp()[2] = 0xa0a0a0a0; vfp()[5] = 0xb5b5b5b5;
	check("vstmdb", run(0xed241a04), 1);
	check("vstmdb s2", word_at(2), 0xa0a0a0a0);
	check("vstmdb s5", word_at(14), 0xb5b5b5b5);
	check("vstmdb wb", R(4), A(2));

	/* ldrd r4, r5, [r6, #6] */
	reset(); R(6) = A(0);
	check("ldrd imm", run(0xe1c640d6), 1);
	check("ldrd r4", R(4), word_at(6));
	check("ldrd r5", R(5), word_at(10));
	check("ldrd r6 kept", R(6), A(0));

	/* strd r2, r3, [r0], #-4 (post-indexed) */
	reset(); R(0) = A(5); R(2) = 0xdeadbeef; R(3) = 0xcafef00d;
	check("strd post", run(0xe04020f4), 1);
	check("strd r2", word_at(5), 0xdeadbeef);
	check("strd r3", word_at(9), 0xcafef00d);
	check("strd wb", R(0), A(1));

	/* ldrd r0, r1, [r2, r3]! */
	reset(); R(2) = A(1); R(3) = 2;
	check("ldrd reg", run(0xe1a200d3), 1);
	check("ldrd reg r0", R(0), word_at(3));
	check("ldrd reg r1", R(1), word_at(7));
	check("ldrd reg wb", R(2), A(3));

	/* ldmia r0!, {r1, r2, r5} */
	reset(); R(0) = A(2);
	check("ldmia", run(0xe8b00026), 1);
	check("ldmia r1", R(1), word_at(2));
	check("ldmia r5", R(5), word_at(10));
	check("ldmia wb", R(0), A(14));

	/* stmdb r1, {r2-r4} (no writeback) */
	reset(); R(1) = A(14); R(2) = 0x22222222; R(4) = 0x44444444;
	check("stmdb", run(0xe901001c), 1);
	check("stmdb r2", word_at(2), 0x22222222);
	check("stmdb r4", word_at(10), 0x44444444);
	check("stmdb r1 kept", R(1), A(14));

	/* ldmib r3, {r4, pc}: pc loaded, to Thumb code */
	reset(); R(3) = A(1); { uint32_t target = 0x88001235; memcpy(M(9), &target, 4); }
	check("ldmib pc", run(0xe9938010), 1);
	check("ldmib r4", R(4), word_at(5));
	check("ldmib pc value", context.uc_mcontext.arm_pc, 0x88001234);
	check("ldmib thumb", context.uc_mcontext.arm_cpsr & 0x20, 0x20);

	/* vldrne s0, [r1] with Z set: not executed, stepped over */
	reset(); R(1) = A(2); context.uc_mcontext.arm_cpsr |= 1u << 30;
	check("vldrne skipped", run(0x1d910a00), 1);
	check("vldrne s0 untouched", vfp()[0], 0);
	check("vldrne pc", context.uc_mcontext.arm_pc, PC0() + 4);
	/* ... and with Z clear, executed */
	reset(); R(1) = A(2);
	check("vldrne taken", run(0x1d910a00), 1);
	check("vldrne value", vfp()[0], word_at(2));

	/* not ours: ldrsh, ldrh, and a Thumb fault, and a fault outside the image */
	reset(); R(1) = A(1);
	check("ldrsh refused", run(0xe1d100f2), 0);
	check("ldrh refused", run(0xe1d100b0), 0);
	context.uc_mcontext.arm_cpsr |= 0x20;
	check("thumb refused", run(0xed990a00), 0);
	reset(); R(9) = A(2); code[0] = 0xed990a00; context.uc_mcontext.arm_pc = (uint32_t)(uintptr_t)memory; info.si_code = 1;
	check("outside refused", host_alignment_fixup(&info, &context), 0);

	/* vldr d16, [r7, #-1020] */
	reset(); R(7) = A(1020 + 3) - 64 + 64; R(7) = (uint32_t)(uintptr_t)(memory + 64 + 3) + 1020;
	check("vldr d16", run(0xed570bff), 1);
	check("vldr d16 low", vfp()[32], word_at(3));
	check("vldr d16 high", vfp()[33], word_at(7));

	sys_write(failures ? "FAILED\n" : "all passed\n");
	hex((uint32_t)tests); sys_write(" checks\n");
	sys_exit(failures != 0);
}
