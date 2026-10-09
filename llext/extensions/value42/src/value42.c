/*
 * LLEXT extension providing the ExtensionLoaderCluster's ExtensionValue
 * attribute: the cluster calls get_value() on every read of that attribute.
 * This one returns a constant; see ../value1337 for a second extension with a
 * different value (scripts/test-llext-values.py loads both in turn).
 */

#include <zephyr/kernel.h>
#include <zephyr/llext/symbol.h>

void run(void)
{
	printk("value42 extension loaded\n");
}
LL_EXTENSION_SYMBOL(run);

int32_t get_value(void)
{
	return 42;
}
LL_EXTENSION_SYMBOL(get_value);
