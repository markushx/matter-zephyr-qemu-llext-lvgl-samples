/*
 * Trivial LLEXT extension: Milestone 3 Phase 0 smoke test. Proves Zephyr's
 * LLEXT loader works at all on qemu_x86 (no existing upstream sample
 * targets this board/arch) before any Matter-side transport is built on
 * top of it.
 */

#include <zephyr/kernel.h>
#include <zephyr/llext/symbol.h>

void run(void)
{
	printk("hello from llext\n");
}
LL_EXTENSION_SYMBOL(run);
