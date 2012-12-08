#ifdef CONFIG_ARM_FCSE_GUARANTEED
#define MAP_BRK	0x40
#endif /* CONFIG_ARM_FCSE_GUARANTEED */

#include <asm-generic/mman.h>

#define arch_mmap_check(addr, len, flags) \
	(((flags) & MAP_FIXED && (addr) < FIRST_USER_ADDRESS) ? -EINVAL : 0)
