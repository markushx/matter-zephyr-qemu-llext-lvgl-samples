/*
 * LLEXT extension adding an endpoint with a cluster the app has never heard
 * of: a manufacturer-specific "Counter" cluster, with its own attributes and
 * its own commands, all implemented here. See
 * include/matter_dynamic_extension.h for the contract.
 *
 * Counter cluster 0xFFF1FC02 (test vendor 0xFFF1):
 *
 *   attribute 0  Count  uint32, read-only
 *   attribute 1  Step   uint8, writable (1..100): what Increment adds
 *   command   0  Increment  optional field 0 (uint): add this much instead
 *                           of Step; more than 1000 is a ConstraintError
 *   command   1  Reset      Count back to 0
 */

#include <errno.h>
#include <string.h>

#include <zephyr/sys/printk.h>
#include <zephyr/sys/util.h>
#include <zephyr/llext/symbol.h>

#include <matter_dynamic_extension.h>

/* Also manufacturer-specific: there is no standard device type for this. */
#define DEVICE_TYPE_COUNTER 0xFFF10001

#define CLUSTER_COUNTER   0xFFF1FC02
#define ATTRIBUTE_COUNT   0x0000
#define ATTRIBUTE_STEP    0x0001
#define COMMAND_INCREMENT 0x00
#define COMMAND_RESET     0x01

/* Interaction Model status codes, see the Matter core specification. */
#define STATUS_UNSUPPORTED_COMMAND 0x81
#define STATUS_CONSTRAINT_ERROR    0x87

#define MAX_INCREMENT 1000

static uint32_t count;
static uint8_t step = 1;

static int counter_read(uint16_t endpoint, uint32_t cluster, uint32_t attribute, uint8_t *buf,
			uint16_t len)
{
	switch (attribute) {
	case ATTRIBUTE_COUNT:
		memcpy(buf, &count, sizeof(count));
		return 0;
	case ATTRIBUTE_STEP:
		buf[0] = step;
		return 0;
	default:
		return -ENOENT;
	}
}

static int counter_write(uint16_t endpoint, uint32_t cluster, uint32_t attribute,
			 const uint8_t *buf, uint16_t len)
{
	if (attribute != ATTRIBUTE_STEP) {
		return -ENOENT;
	}
	if (buf[0] < 1 || buf[0] > 100) {
		return STATUS_CONSTRAINT_ERROR;
	}
	step = buf[0];
	return 0;
}

static int counter_invoke(uint16_t endpoint, uint32_t cluster, uint32_t command,
			  const struct matter_dyn_arg *args, uint8_t arg_count)
{
	uint32_t by = step;

	switch (command) {
	case COMMAND_INCREMENT:
		for (uint8_t i = 0; i < arg_count; i++) {
			if (args[i].tag == 0 && args[i].type == MATTER_DYN_ARG_UINT) {
				if (args[i].value > MAX_INCREMENT) {
					return STATUS_CONSTRAINT_ERROR;
				}
				by = (uint32_t)args[i].value;
			}
		}
		count += by;
		break;
	case COMMAND_RESET:
		count = 0;
		break;
	default:
		return STATUS_UNSUPPORTED_COMMAND;
	}

	printk("counter: endpoint %u count is now %u\n", endpoint, count);
	/* Count changed without anybody writing it: report it. */
	matter_dyn_attribute_changed(endpoint, CLUSTER_COUNTER, ATTRIBUTE_COUNT);
	return 0;
}

static const struct matter_dyn_attribute counter_attributes[] = {
	{ ATTRIBUTE_COUNT, MATTER_DYN_UINT32, 0 },
	{ ATTRIBUTE_STEP, MATTER_DYN_UINT8, MATTER_DYN_WRITABLE },
};

static const uint32_t counter_commands[] = { COMMAND_INCREMENT, COMMAND_RESET };

static const struct matter_dyn_cluster clusters[] = {
	{
		.id = CLUSTER_COUNTER,
		.revision = 1,
		.attributes = counter_attributes,
		.attribute_count = ARRAY_SIZE(counter_attributes),
		.commands = counter_commands,
		.command_count = ARRAY_SIZE(counter_commands),
		.read = counter_read,
		.write = counter_write,
		.invoke = counter_invoke,
	},
};

static const struct matter_dyn_endpoint counter = {
	.device_type = DEVICE_TYPE_COUNTER,
	.device_type_revision = 1,
	.name = "LLEXT counter",
	.clusters = clusters,
	.cluster_count = ARRAY_SIZE(clusters),
};

void run(void)
{
	int endpoint = matter_dyn_endpoint_add(&counter);

	if (endpoint < 0) {
		printk("counter: adding the endpoint failed: %d\n", endpoint);
		return;
	}
	printk("counter: counter on endpoint %d\n", endpoint);
}
LL_EXTENSION_SYMBOL(run);
