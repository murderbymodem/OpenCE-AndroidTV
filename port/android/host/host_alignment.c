/*
HOST_ALIGNMENT.C

Unaligned accesses of the 32-bit ARM guest (armeabi-v7a).

The game was written for x86, which loads and stores at any address, and
the AArch64 guest's processor does too. 32-bit ARM loads and stores single
words and halfwords at any address, but stops a program with SIGBUS
(BUS_ADRALN) at a floating-point load or store (VLDR, VSTR, VLDM, VSTM), a
double-word one (LDRD, STRD) or a multiple one (LDM, STM) whose address is
not a multiple of 4. The game makes such accesses wherever its data is
packed: the script engine's stack keeps values 2 bytes apart, and the maps'
tags have fields at any offset. Linux on 32-bit ARM can fix such accesses up
for a program; Android's kernels running 32-bit apps do not.

So the host does it, as the kernel would: the SIGBUS handler (host_memory.c)
passes a guest alignment fault here, which decodes the faulting ARM-state
instruction, performs its loads and stores a byte at a time on the
registers saved in the signal's context (the core registers in uc_mcontext,
the VFP registers in the VFP frame in uc_regspace, which the kernel restores
on return) and steps over it. Anything else (Thumb code, the host's own,
NEON's element loads, exclusive accesses) is left to crash as before.

A fixup costs a signal, some microseconds. The first one at each place is
logged with its address, and again at 1000, 100000 and 10000000, so that
the places that make many can be found (llvm-addr2line, or llvm-symbolizer
--obj=halo_guest_armv7.elf) and their code made to access the data in
aligned pieces instead.
*/

#include "host.h"

#ifdef __arm__

#include <signal.h>
#include <string.h>
#include <ucontext.h>

/* the kernel's VFP frame in uc_regspace (arch/arm/include/asm/ucontext.h:
struct vfp_sigframe, whose struct user_vfp starts with the 32 double
registers) */
#define VFP_MAGIC 0x56465001u
#define VFP_FRAME_REGISTERS_OFFSET 8

#define CPSR_THUMB 0x20u
#define PC_REGISTER 15

/* the places fixed, and how many times each */
#define MAXIMUM_SITES 256

struct site
{
	uint32_t pc;
	uint32_t count;
};

static struct site sites[MAXIMUM_SITES];
static volatile uint32_t site_count;
static volatile uint32_t total_fixups;

static uint32_t *core_register(struct sigcontext *registers, unsigned int index)
{
	/* (unsigned long, 32 bits here) */
	return (uint32_t *)&(&registers->arm_r0)[index];
}

/* a register as an operand: the PC reads as the instruction's address + 8 */
static uint32_t operand(struct sigcontext *registers, unsigned int index)
{
	if (index == PC_REGISTER)
		return registers->arm_pc + 8;
	return *core_register(registers, index);
}

/* the 32 double registers (64 single registers alias their halves) of the
VFP frame, or NULL if there is none */
static unsigned char *vfp_registers(ucontext_t *context)
{
	unsigned char *frame = (unsigned char *)context->uc_regspace;
	unsigned char *end = frame + sizeof(context->uc_regspace);

	while (frame + 8 <= end)
	{
		uint32_t magic, size;

		memcpy(&magic, frame, 4);
		memcpy(&size, frame + 4, 4);
		if (!magic || size < 8 || frame + size > end)
			return NULL;
		if (magic == VFP_MAGIC)
			return frame + VFP_FRAME_REGISTERS_OFFSET;
		frame += size;
	}
	return NULL;
}

static int condition_passed(uint32_t condition, uint32_t cpsr)
{
	int n = (cpsr >> 31) & 1, z = (cpsr >> 30) & 1, c = (cpsr >> 29) & 1, v = (cpsr >> 28) & 1;
	int result;

	switch (condition >> 1)
	{
	case 0: result = z; break;
	case 1: result = c; break;
	case 2: result = n; break;
	case 3: result = v; break;
	case 4: result = c && !z; break;
	case 5: result = n == v; break;
	case 6: result = !z && n == v; break;
	default: return 1; /* AL */
	}
	return (condition & 1) ? !result : result;
}

/* loads or stores count words at address, to or from the core registers
or the VFP registers */
static void transfer(unsigned char *memory, unsigned char *registers, uint32_t bytes, int load)
{
	if (load)
		memcpy(registers, memory, bytes);
	else
		memcpy(memory, registers, bytes);
}

/* VLDR, VSTR, VLDM, VSTM (and VPUSH, VPOP): cccc 110P UDWL nnnn dddd 101s iiiiiiii */
static int fix_vfp(ucontext_t *context, uint32_t instruction)
{
	struct sigcontext *registers = &context->uc_mcontext;
	unsigned char *vfp = vfp_registers(context);
	int p = (instruction >> 24) & 1, u = (instruction >> 23) & 1, d = (instruction >> 22) & 1;
	int w = (instruction >> 21) & 1, load = (instruction >> 20) & 1, is_double = (instruction >> 8) & 1;
	unsigned int n = (instruction >> 16) & 15, vd = (instruction >> 12) & 15;
	uint32_t words = instruction & 0xff;
	uint32_t base, address, count, index;
	unsigned int first;

	if (!vfp || (!p && !u) || (p && w && u))
		return 0; /* (64-bit core register moves, or undefined) */
	first = is_double ? (unsigned int)(d << 4) | vd : (vd << 1) | (unsigned int)d;
	base = n == PC_REGISTER ? (registers->arm_pc + 8) & ~3u : *core_register(registers, n);
	if (p && !w)
	{
		/* VLDR, VSTR: one register at base +/- words * 4 */
		address = u ? base + words * 4 : base - words * 4;
		count = 1;
	}
	else
	{
		/* VLDM, VSTM: increment after, or decrement before */
		address = p ? base - words * 4 : base;
		count = is_double ? words / 2 : words;
		if (w)
			*core_register(registers, n) = u ? base + words * 4 : base - words * 4;
	}
	for (index = 0; index < count; index++)
	{
		unsigned int number = first + index;

		if (is_double)
		{
			if (number >= 32)
				return 0;
			transfer((unsigned char *)(uintptr_t)(address + index * 8), vfp + number * 8, 8, load);
		}
		else
		{
			if (number >= 64)
				return 0;
			transfer((unsigned char *)(uintptr_t)(address + index * 4), vfp + (number >> 1) * 8 + (number & 1) * 4,
				4, load);
		}
	}
	return 1;
}

/* LDRD, STRD: cccc 000P U1W0 nnnn tttt iiii 11s1 iiii (immediate) or
cccc 000P U0W0 nnnn tttt 0000 11s1 mmmm (register) */
static int fix_doubleword(struct sigcontext *registers, uint32_t instruction)
{
	int p = (instruction >> 24) & 1, u = (instruction >> 23) & 1, immediate = (instruction >> 22) & 1;
	int w = (instruction >> 21) & 1, load = ((instruction >> 5) & 1) == 0;
	unsigned int n = (instruction >> 16) & 15, t = (instruction >> 12) & 15;
	uint32_t offset, base, address;
	unsigned char *memory;

	if ((t & 1) || t == 14 || (!p && w))
		return 0;
	offset = immediate ? ((instruction >> 4) & 0xf0) | (instruction & 0xf) : operand(registers, instruction & 15);
	base = operand(registers, n);
	address = p ? (u ? base + offset : base - offset) : base;
	memory = (unsigned char *)(uintptr_t)address;
	if (load)
	{
		memcpy(core_register(registers, t), memory, 4);
		memcpy(core_register(registers, t + 1), memory + 4, 4);
	}
	else
	{
		uint32_t low = operand(registers, t), high = operand(registers, t + 1);

		memcpy(memory, &low, 4);
		memcpy(memory + 4, &high, 4);
	}
	if ((!p || w) && n != PC_REGISTER && !(load && (n == t || n == t + 1)))
		*core_register(registers, n) = u ? base + offset : base - offset;
	return 1;
}

/* LDM, STM (and PUSH, POP): cccc 100P USWL nnnn rrrrrrrrrrrrrrrr; returns 2
when it loaded the PC */
static int fix_multiple(struct sigcontext *registers, uint32_t instruction)
{
	int p = (instruction >> 24) & 1, u = (instruction >> 23) & 1, s = (instruction >> 22) & 1;
	int w = (instruction >> 21) & 1, load = (instruction >> 20) & 1;
	unsigned int n = (instruction >> 16) & 15;
	uint32_t list = instruction & 0xffff;
	uint32_t base = *core_register(registers, n), count = (uint32_t)__builtin_popcount(list);
	uint32_t address, value;
	unsigned int index;
	int loaded_pc = 0;

	if (s || !list || n == PC_REGISTER)
		return 0;
	if (u)
		address = p ? base + 4 : base;
	else
		address = p ? base - count * 4 : base - count * 4 + 4;
	for (index = 0; index < 16; index++)
	{
		unsigned char *memory;

		if (!(list & (1u << index)))
			continue;
		memory = (unsigned char *)(uintptr_t)address;
		if (load)
		{
			memcpy(&value, memory, 4);
			if (index == PC_REGISTER)
			{
				/* (interworking: bit 0 chooses Thumb) */
				if (value & 1)
					registers->arm_cpsr |= CPSR_THUMB;
				registers->arm_pc = value & ~1u;
				loaded_pc = 1;
			}
			else
			{
				*core_register(registers, index) = value;
			}
		}
		else
		{
			value = index == PC_REGISTER ? registers->arm_pc + 8 : *core_register(registers, index);
			memcpy(memory, &value, 4);
		}
		address += 4;
	}
	if (w && !(load && (list & (1u << n))))
		*core_register(registers, n) = u ? base + count * 4 : base - count * 4;
	return loaded_pc ? 2 : 1;
}

static void note_site(uint32_t pc)
{
	uint32_t index, count = site_count;
	uint32_t times = 0;

	__sync_add_and_fetch(&total_fixups, 1);
	for (index = 0; index < count && index < MAXIMUM_SITES; index++)
	{
		if (sites[index].pc == pc)
		{
			times = __sync_add_and_fetch(&sites[index].count, 1);
			break;
		}
	}
	if (index == count && count < MAXIMUM_SITES)
	{
		/* (a site two threads add at once is listed twice: harmless) */
		index = __sync_fetch_and_add(&site_count, 1);
		if (index < MAXIMUM_SITES)
		{
			sites[index].pc = pc;
			sites[index].count = times = 1;
		}
	}
	if (times == 1 || times == 1000 || times == 100000 || times == 10000000)
	{
		host_logf(HOST_LOG_WARN, "unaligned access fixed at %08x (guest %08x), %u times there, %u in all",
			pc, pc - host_image.base, times, total_fixups);
	}
}

int host_alignment_fixup(siginfo_t *information, void *context_pointer)
{
	ucontext_t *context = context_pointer;
	struct sigcontext *registers = &context->uc_mcontext;
	uint32_t pc = registers->arm_pc, instruction;
	int result;

	if (information->si_code != BUS_ADRALN || (registers->arm_cpsr & CPSR_THUMB) ||
		pc < host_image.base || pc + 4 > host_image.end || (pc & 3))
		return 0;
	memcpy(&instruction, (const void *)(uintptr_t)pc, 4);
	if ((instruction >> 28) == 0xf)
		return 0; /* (NEON's element loads and other unconditional instructions) */
	if (!condition_passed(instruction >> 28, registers->arm_cpsr))
	{
		registers->arm_pc = pc + 4;
		return 1;
	}
	if ((instruction & 0x0e000e00) == 0x0c000a00)
		result = fix_vfp(context, instruction);
	else if ((instruction & 0x0e4000d0) == 0x004000d0 || (instruction & 0x0e400fd0) == 0x000000d0)
		result = ((instruction >> 20) & 1) ? 0 : fix_doubleword(registers, instruction);
	else if ((instruction & 0x0e000000) == 0x08000000)
		result = fix_multiple(registers, instruction);
	else
		result = 0;
	if (!result)
	{
		host_logf(HOST_LOG_ERROR, "unaligned access at %08x (guest %08x): instruction %08x is not one the host fixes",
			pc, pc - host_image.base, instruction);
		return 0;
	}
	if (result != 2)
		registers->arm_pc = pc + 4;
	note_site(pc);
	return 1;
}

#endif
