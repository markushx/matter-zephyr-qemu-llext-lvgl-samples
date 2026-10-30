/*
 * LLEXT extension adding a bridged On/Off Light endpoint. See
 * include/matter_dynamic_extension.h for the contract.
 *
 * The light only stores its state: the On, Off and Toggle commands are
 * handled by the app's built-in On/Off cluster server (`invoke` is NULL),
 * which turns them into a write of the OnOff attribute. ../counter shows an
 * extension that handles commands itself.
 */

#include <errno.h>

#include <zephyr/sys/printk.h>
#include <zephyr/sys/util.h>
#include <zephyr/llext/symbol.h>

#include <matter_dynamic_extension.h>

#define DEVICE_TYPE_ON_OFF_LIGHT 0x0100

#define CLUSTER_ON_OFF  0x0006
#define ATTRIBUTE_ON_OFF 0x0000
#define COMMAND_OFF     0x00
#define COMMAND_ON      0x01
#define COMMAND_TOGGLE  0x02

static bool on;

static int light_read(uint16_t endpoint, uint32_t cluster, uint32_t attribute, uint8_t *buf,
		      uint16_t len)
{
	if (attribute != ATTRIBUTE_ON_OFF) {
		return -ENOENT;
	}
	buf[0] = on;
	return 0;
}

static int light_write(uint16_t endpoint, uint32_t cluster, uint32_t attribute,
		       const uint8_t *buf, uint16_t len)
{
	if (attribute != ATTRIBUTE_ON_OFF) {
		return -ENOENT;
	}
	on = buf[0] != 0;
	printk("onoff_light: endpoint %u is now %s\n", endpoint, on ? "ON" : "OFF");
	return 0;
}

static const struct matter_dyn_attribute on_off_attributes[] = {
	{ ATTRIBUTE_ON_OFF, MATTER_DYN_BOOL, 0 },
};

static const uint32_t on_off_commands[] = { COMMAND_OFF, COMMAND_ON, COMMAND_TOGGLE };

static const struct matter_dyn_cluster clusters[] = {
	{
		.id = CLUSTER_ON_OFF,
		.revision = 6,
		.attributes = on_off_attributes,
		.attribute_count = ARRAY_SIZE(on_off_attributes),
		.commands = on_off_commands,
		.command_count = ARRAY_SIZE(on_off_commands),
		.read = light_read,
		.write = light_write,
	},
};

static const struct matter_dyn_endpoint light = {
	.device_type = DEVICE_TYPE_ON_OFF_LIGHT,
	.device_type_revision = 3,
	.name = "LLEXT light",
	.clusters = clusters,
	.cluster_count = ARRAY_SIZE(clusters),
};

void run(void)
{
	int endpoint = matter_dyn_endpoint_add(&light);

	if (endpoint < 0) {
		printk("onoff_light: adding the endpoint failed: %d\n", endpoint);
		return;
	}
	printk("onoff_light: on/off light on endpoint %d\n", endpoint);
}
LL_EXTENSION_SYMBOL(run);
