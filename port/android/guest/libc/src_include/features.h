/*
musl's internal features.h, with weak_alias spelled as assembler
directives: the guest is compiled for a Darwin target, which rejects alias
attributes, and its assembly is converted to ELF afterwards
(tools/android_asm_convert.py), where the alias is ordinary. The 32-bit
ARM guest is compiled for ELF, and keeps musl's own.
*/

#ifndef GUEST_FEATURES_H
#define GUEST_FEATURES_H

#include_next "features.h"

#ifndef HALO_ARM32_GUEST
#undef weak_alias
#define weak_alias(old, new) \
	extern __typeof(old) new; \
	static void *const __guest_alias_##new __attribute__((used)) = (void *)&old; \
	__asm__(".globl _" #new "\n.weak_definition _" #new "\n.set _" #new ", _" #old "\n")
#endif

#endif
