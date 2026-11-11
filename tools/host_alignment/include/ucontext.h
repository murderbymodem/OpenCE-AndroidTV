struct sigcontext {
	unsigned long trap_no, error_code, oldmask;
	unsigned long arm_r0, arm_r1, arm_r2, arm_r3, arm_r4, arm_r5, arm_r6, arm_r7, arm_r8, arm_r9, arm_r10;
	unsigned long arm_fp, arm_ip, arm_sp, arm_lr, arm_pc, arm_cpsr, fault_address;
};
typedef struct { unsigned long uc_flags; void *uc_link; struct { void *a; int b; unsigned c; } uc_stack;
	struct sigcontext uc_mcontext; char sigmask[128];
	unsigned long uc_regspace[128] __attribute__((aligned(8))); } ucontext_t;
