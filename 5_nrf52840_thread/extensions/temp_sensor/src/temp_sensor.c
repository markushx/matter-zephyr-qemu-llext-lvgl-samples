/*
 * LLEXT extension adding a bridged Temperature Sensor endpoint with a
 * simulated measurement. See include/matter_dynamic_extension.h for the
 * contract.
 *
 * Shows a value that changes on the extension's own initiative: poll() --
 * called by the app about once per second -- moves the temperature and
 * reports the change, so controllers subscribed to MeasuredValue get
 * updates.
 */

#include <errno.h>
#include <string.h>

#include <zephyr/sys/printk.h>
#include <zephyr/sys/util.h>
#include <zephyr/llext/symbol.h>

#include <matter_dynamic_extension.h>

#define DEVICE_TYPE_TEMPERATURE_SENSOR 0x0302

#define CLUSTER_TEMPERATURE_MEASUREMENT 0x0402
#define ATTRIBUTE_MEASURED_VALUE        0x0000
#define ATTRIBUTE_MIN_MEASURED_VALUE    0x0001
#define ATTRIBUTE_MAX_MEASURED_VALUE    0x0002

/* All in units of 0.01 degrees Celsius, as the cluster defines them. */
#define TEMPERATURE_MIN  2000
#define TEMPERATURE_MAX  2500
#define TEMPERATURE_STEP 25

static int endpoint_id = -1;
static int16_t temperature = TEMPERATURE_MIN;
static int16_t step = TEMPERATURE_STEP;

static int sensor_read(uint16_t endpoint, uint32_t cluster, uint32_t attribute, uint8_t *buf,
		       uint16_t len)
{
	int16_t value;

	switch (attribute) {
	case ATTRIBUTE_MEASURED_VALUE:
		value = temperature;
		break;
	case ATTRIBUTE_MIN_MEASURED_VALUE:
		value = TEMPERATURE_MIN;
		break;
	case ATTRIBUTE_MAX_MEASURED_VALUE:
		value = TEMPERATURE_MAX;
		break;
	default:
		return -ENOENT;
	}
	memcpy(buf, &value, sizeof(value));
	return 0;
}

static const struct matter_dyn_attribute temperature_attributes[] = {
	{ ATTRIBUTE_MEASURED_VALUE, MATTER_DYN_INT16, MATTER_DYN_NULLABLE },
	{ ATTRIBUTE_MIN_MEASURED_VALUE, MATTER_DYN_INT16, MATTER_DYN_NULLABLE },
	{ ATTRIBUTE_MAX_MEASURED_VALUE, MATTER_DYN_INT16, MATTER_DYN_NULLABLE },
};

static const struct matter_dyn_cluster clusters[] = {
	{
		.id = CLUSTER_TEMPERATURE_MEASUREMENT,
		.revision = 4,
		.attributes = temperature_attributes,
		.attribute_count = ARRAY_SIZE(temperature_attributes),
		.read = sensor_read,
	},
};

static const struct matter_dyn_endpoint sensor = {
	.device_type = DEVICE_TYPE_TEMPERATURE_SENSOR,
	.device_type_revision = 2,
	.name = "LLEXT thermometer",
	.clusters = clusters,
	.cluster_count = ARRAY_SIZE(clusters),
};

void run(void)
{
	endpoint_id = matter_dyn_endpoint_add(&sensor);
	if (endpoint_id < 0) {
		printk("temp_sensor: adding the endpoint failed: %d\n", endpoint_id);
		return;
	}
	printk("temp_sensor: temperature sensor on endpoint %d\n", endpoint_id);
}
LL_EXTENSION_SYMBOL(run);

/* A slow triangle wave between TEMPERATURE_MIN and TEMPERATURE_MAX. */
void poll(void)
{
	if (endpoint_id < 0) {
		return;
	}

	if (temperature + step > TEMPERATURE_MAX || temperature + step < TEMPERATURE_MIN) {
		step = -step;
	}
	temperature += step;
	matter_dyn_attribute_changed(endpoint_id, CLUSTER_TEMPERATURE_MEASUREMENT,
				     ATTRIBUTE_MEASURED_VALUE);
}
LL_EXTENSION_SYMBOL(poll);
