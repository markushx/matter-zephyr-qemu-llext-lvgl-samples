/*
 * Contract between the 4_llext_dynamic app and an extension loaded through
 * the ExtensionLoaderCluster: how an extension adds Matter endpoints --
 * with their clusters, attributes and command handlers -- to the running
 * device.
 *
 * The app is a Matter bridge (Aggregator on endpoint 1); every endpoint an
 * extension adds shows up as a bridged device below it, the same way
 * connectedhomeip's examples/bridge-app and nRF Connect's bridge sample
 * expose theirs. The difference is that the "bridged device" implementations
 * are not compiled into the app but arrive at runtime as extensions.
 *
 * An extension describes an endpoint with the const tables below and calls
 * matter_dyn_endpoint_add() from its run() function. From then on the app
 * calls back into the extension whenever a controller reads or writes one of
 * the declared attributes or invokes one of the declared commands.
 *
 * Symbols an extension exports (LL_EXTENSION_SYMBOL), all optional:
 *
 *   void run(void);   called once, right after loading
 *   void poll(void);  called about once per second while loaded
 *
 * Everything the app calls -- run(), poll() and the three cluster callbacks
 * -- runs on the Matter thread, one call at a time, and the functions
 * declared at the bottom of this header may only be called from inside those
 * calls (adding and removing endpoints only from run() and poll()). An
 * extension therefore needs no locking, and must not block.
 *
 * When an extension is unloaded, the app removes all endpoints it added;
 * there is no cleanup hook. The tables passed to matter_dyn_endpoint_add()
 * (and everything they point to) must stay valid for as long as the
 * extension is loaded -- make them static.
 */

#ifndef MATTER_DYNAMIC_EXTENSION_H_
#define MATTER_DYNAMIC_EXTENSION_H_

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Attribute data types. Values travel as fixed-size little-endian buffers. */
enum matter_dyn_type {
	MATTER_DYN_BOOL,     /* 1 byte */
	MATTER_DYN_UINT8,    /* 1 byte */
	MATTER_DYN_UINT16,   /* 2 bytes */
	MATTER_DYN_UINT32,   /* 4 bytes */
	MATTER_DYN_INT8,     /* 1 byte */
	MATTER_DYN_INT16,    /* 2 bytes */
	MATTER_DYN_INT32,    /* 4 bytes */
	MATTER_DYN_ENUM8,    /* 1 byte */
	MATTER_DYN_BITMAP8,  /* 1 byte */
	MATTER_DYN_BITMAP32, /* 4 bytes */
};

/* Attribute flags. */
#define MATTER_DYN_WRITABLE 0x01
/*
 * A nullable attribute reports null by returning its type's reserved value:
 * 0xFF.. for unsigned types, the most negative value for signed ones (e.g.
 * INT16_MIN), 0xFF for bool/enum8.
 */
#define MATTER_DYN_NULLABLE 0x02

struct matter_dyn_attribute {
	uint32_t id;
	uint8_t type;  /* enum matter_dyn_type */
	uint8_t flags; /* MATTER_DYN_WRITABLE | MATTER_DYN_NULLABLE */
};

/* One field of an invoked command, as decoded by the app. */
enum matter_dyn_arg_type {
	MATTER_DYN_ARG_NULL,
	MATTER_DYN_ARG_BOOL,
	MATTER_DYN_ARG_UINT,
	MATTER_DYN_ARG_INT,
};

struct matter_dyn_arg {
	uint8_t tag;  /* the field's context tag, i.e. its field ID */
	uint8_t type; /* enum matter_dyn_arg_type */
	int64_t value;
};

/* At most this many fields of a command are passed on; the rest is dropped. */
#define MATTER_DYN_MAX_ARGS 8

/*
 * Callback results: 0 for success; a positive value is sent to the controller
 * as that Interaction Model status code (e.g. 0x87 ConstraintError); any
 * negative value becomes a generic Failure.
 */

/* Write the attribute's current value to buf (len is its type's size). */
typedef int (*matter_dyn_read_fn)(uint16_t endpoint, uint32_t cluster, uint32_t attribute,
				  uint8_t *buf, uint16_t len);

/* A controller (or one of the app's own cluster servers) set a new value. */
typedef int (*matter_dyn_write_fn)(uint16_t endpoint, uint32_t cluster, uint32_t attribute,
				   const uint8_t *buf, uint16_t len);

/* A controller invoked one of the cluster's commands. */
typedef int (*matter_dyn_invoke_fn)(uint16_t endpoint, uint32_t cluster, uint32_t command,
				    const struct matter_dyn_arg *args, uint8_t arg_count);

struct matter_dyn_cluster {
	uint32_t id;
	/* The global ClusterRevision and FeatureMap attributes; served by the
	 * app, don't list them in `attributes`.
	 */
	uint16_t revision;
	uint32_t feature_map;

	const struct matter_dyn_attribute *attributes;
	uint8_t attribute_count;

	/* IDs of the commands the cluster accepts. */
	const uint32_t *commands;
	uint8_t command_count;

	matter_dyn_read_fn read;
	matter_dyn_write_fn write; /* may be NULL if nothing is writable */
	/*
	 * NULL: commands go to the app's built-in server for this cluster,
	 * if it has one. That is the case for On/Off, whose server turns
	 * On/Off/Toggle into a write of the OnOff attribute -- so an on/off
	 * light only needs read and write. Set it to handle the commands in
	 * the extension instead, which is the only option for clusters the
	 * app knows nothing about.
	 */
	matter_dyn_invoke_fn invoke;
};

struct matter_dyn_endpoint {
	/* Matter device type, e.g. 0x0100 (On/Off Light), and its revision. */
	uint32_t device_type;
	uint8_t device_type_revision;
	/* NodeLabel of the bridged device, at most 32 characters. */
	const char *name;

	const struct matter_dyn_cluster *clusters;
	uint8_t cluster_count;
};

/*
 * Adds an endpoint. Only from run() or poll(). Returns its endpoint ID
 * (> 0), or a negative errno: -EINVAL for a malformed description or a call
 * from elsewhere, -ENOMEM, or -ENOSPC if all dynamic endpoints are in use.
 */
int matter_dyn_endpoint_add(const struct matter_dyn_endpoint *endpoint);

/*
 * Removes an endpoint this extension added. Only from run() or poll().
 * Optional -- unloading the extension removes them all. Returns 0 or -ENOENT.
 */
int matter_dyn_endpoint_remove(uint16_t endpoint);

/*
 * Tells the app that an attribute's value changed on the extension's own
 * initiative, so that subscribed controllers get a report. Not needed for
 * changes made through the write callback.
 */
void matter_dyn_attribute_changed(uint16_t endpoint, uint32_t cluster, uint32_t attribute);

#ifdef __cplusplus
}
#endif

#endif /* MATTER_DYNAMIC_EXTENSION_H_ */
