/*
 * LLEXT extension providing the ExtensionLoaderCluster's ExtensionValue
 * attribute, like ../value42 but with a different value -- and computed from
 * the extension's own data rather than returned as a literal, so the two
 * binaries differ in more than one immediate operand.
 */

#include <zephyr/kernel.h>
#include <zephyr/llext/symbol.h>

static const int32_t digits[] = { 1, 3, 3, 7 };

void run(void)
{
	printk("value1337 extension loaded\n");
}
LL_EXTENSION_SYMBOL(run);

int32_t get_value(void)
{
	int32_t value = 0;

	for (size_t i = 0; i < ARRAY_SIZE(digits); i++) {
		value = value * 10 + digits[i];
	}
	return value;
}
LL_EXTENSION_SYMBOL(get_value);
