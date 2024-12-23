// SPDX-License-Identifier: GPL-2.0
/*
 * bus.c - bus driver management
 *
 * Copyright (c) 2002-3 Patrick Mochel
 * Copyright (c) 2002-3 Open Source Development Labs
 * Copyright (c) 2007 Greg Kroah-Hartman <gregkh@suse.de>
 * Copyright (c) 2007 Novell Inc.
 */

#include <linux/async.h>
#include <linux/device/bus.h>
#include <linux/device.h>
#include <linux/errno.h>
#include <linux/compat.h>
#include <linux/init.h>
#include <linux/string.h>

#include "base.h"
#include "power/power.h"

/* klist/klist_node -> device/driver/bus */
static int __must_check bus_rescan_devices_helper(struct device *dev,
						void *data);

static struct bus_type *bus_get(struct bus_type *bus)
{
	return bus;
}

static void bus_put(struct bus_type *bus)
{

}

static struct device *next_device(struct klist_iter *i)
{
	struct klist_node *n = klist_next(i);
	struct device *dev = NULL;
	struct device_private *dev_prv;

	if (n) {
		dev_prv = to_device_private_bus(n);
		dev = dev_prv->device;
	}
	return dev;
}

/**
 * bus_for_each_dev - device iterator.
 * @bus: bus type.
 * @start: device to start iterating from.
 * @data: data for the callback.
 * @fn: function to be called for each device.
 *
 * Iterate over @bus's list of devices, and call @fn for each,
 * passing it @data. If @start is not NULL, we use that device to
 * begin iterating from.
 *
 * We check the return of @fn each time. If it returns anything
 * other than 0, we break out and return that value.
 *
 * NOTE: The device that returns a non-zero value is not retained
 * in any way, nor is its refcount incremented. If the caller needs
 * to retain this data, it should do so, and increment the reference
 * count in the supplied callback.
 */
int bus_for_each_dev(struct bus_type *bus, struct device *start,
		     void *data, int (*fn)(struct device *, void *))
{
	struct klist_iter i;
	struct device *dev;
	int error = 0;

	if (!bus || !bus->p)
		return -EINVAL;

	klist_iter_init_node(&bus->p->klist_devices, &i,
			     (start ? &start->p->knode_bus : NULL));
	while (!error && (dev = next_device(&i)))
		error = fn(dev, data);
	klist_iter_exit(&i);
	return error;
}
EXPORT_SYMBOL_GPL(bus_for_each_dev);

/**
 * bus_find_device - device iterator for locating a particular device.
 * @bus: bus type
 * @start: Device to begin with
 * @data: Data to pass to match function
 * @match: Callback function to check device
 *
 * This is similar to the bus_for_each_dev() function above, but it
 * returns a reference to a device that is 'found' for later use, as
 * determined by the @match callback.
 *
 * The callback should return 0 if the device doesn't match and non-zero
 * if it does.  If the callback returns non-zero, this function will
 * return to the caller and not iterate over any more devices.
 */
struct device *bus_find_device(struct bus_type *bus,
			       struct device *start, const void *data,
			       int (*match)(struct device *dev, const void *data))
{
	struct klist_iter i;
	struct device *dev;

	if (!bus || !bus->p)
		return NULL;

	klist_iter_init_node(&bus->p->klist_devices, &i,
			     (start ? &start->p->knode_bus : NULL));
	while ((dev = next_device(&i)))
		if (match(dev, data) && get_device(dev))
			break;
	klist_iter_exit(&i);
	return dev;
}
EXPORT_SYMBOL_GPL(bus_find_device);

/**
 * subsys_find_device_by_id - find a device with a specific enumeration number
 * @subsys: subsystem
 * @id: index 'id' in struct device
 * @hint: device to check first
 *
 * Check the hint's next object and if it is a match return it directly,
 * otherwise, fall back to a full list search. Either way a reference for
 * the returned object is taken.
 */
struct device *subsys_find_device_by_id(struct bus_type *subsys, unsigned int id,
					struct device *hint)
{
	struct klist_iter i;
	struct device *dev;

	if (!subsys)
		return NULL;

	if (hint) {
		klist_iter_init_node(&subsys->p->klist_devices, &i, &hint->p->knode_bus);
		dev = next_device(&i);
		if (dev && dev->id == id && get_device(dev)) {
			klist_iter_exit(&i);
			return dev;
		}
		klist_iter_exit(&i);
	}

	klist_iter_init_node(&subsys->p->klist_devices, &i, NULL);
	while ((dev = next_device(&i))) {
		if (dev->id == id && get_device(dev)) {
			klist_iter_exit(&i);
			return dev;
		}
	}
	klist_iter_exit(&i);
	return NULL;
}
EXPORT_SYMBOL_GPL(subsys_find_device_by_id);

static struct device_driver *next_driver(struct klist_iter *i)
{
	struct klist_node *n = klist_next(i);
	struct driver_private *drv_priv;

	if (n) {
		drv_priv = container_of(n, struct driver_private, knode_bus);
		return drv_priv->driver;
	}
	return NULL;
}

/**
 * bus_for_each_drv - driver iterator
 * @bus: bus we're dealing with.
 * @start: driver to start iterating on.
 * @data: data to pass to the callback.
 * @fn: function to call for each driver.
 *
 * This is nearly identical to the device iterator above.
 * We iterate over each driver that belongs to @bus, and call
 * @fn for each. If @fn returns anything but 0, we break out
 * and return it. If @start is not NULL, we use it as the head
 * of the list.
 *
 * NOTE: we don't return the driver that returns a non-zero
 * value, nor do we leave the reference count incremented for that
 * driver. If the caller needs to know that info, it must set it
 * in the callback. It must also be sure to increment the refcount
 * so it doesn't disappear before returning to the caller.
 */
int bus_for_each_drv(struct bus_type *bus, struct device_driver *start,
		     void *data, int (*fn)(struct device_driver *, void *))
{
	struct klist_iter i;
	struct device_driver *drv;
	int error = 0;

	if (!bus)
		return -EINVAL;

	klist_iter_init_node(&bus->p->klist_drivers, &i,
			     start ? &start->p->knode_bus : NULL);
	while ((drv = next_driver(&i)) && !error)
		error = fn(drv, data);
	klist_iter_exit(&i);
	return error;
}
EXPORT_SYMBOL_GPL(bus_for_each_drv);

/**
 * bus_add_device - add device to bus
 * @dev: device being added
 *
 * - Add device's bus attributes.
 * - Create links to device's bus.
 * - Add the device to its bus's list of devices.
 */
int bus_add_device(struct device *dev)
{
	struct bus_type *bus = bus_get(dev->bus);
	int error = 0;

	if (bus) {
		pr_debug("bus: '%s': add device %s\n", bus->name, dev_name(dev));
		klist_add_tail(&dev->p->knode_bus, &bus->p->klist_devices);
	}
	return 0;
}

/**
 * bus_probe_device - probe drivers for a new device
 * @dev: device to probe
 *
 * - Automatically probe for a driver if the bus allows it.
 */
void bus_probe_device(struct device *dev)
{
	struct bus_type *bus = dev->bus;
	struct subsys_interface *sif;

	if (!bus)
		return;

	if (bus->p->drivers_autoprobe)
		device_initial_probe(dev);

	mutex_lock(&bus->p->mutex);
	list_for_each_entry(sif, &bus->p->interfaces, node)
		if (sif->add_dev)
			sif->add_dev(dev, sif);
	mutex_unlock(&bus->p->mutex);
}

/**
 * bus_remove_device - remove device from bus
 * @dev: device to be removed
 *
 * - Remove device from all interfaces.
 * - Remove symlink from bus' directory.
 * - Delete device from bus's list.
 * - Detach from its driver.
 * - Drop reference taken in bus_add_device().
 */
void bus_remove_device(struct device *dev)
{
	struct bus_type *bus = dev->bus;
	struct subsys_interface *sif;

	if (!bus)
		return;

	mutex_lock(&bus->p->mutex);
	list_for_each_entry(sif, &bus->p->interfaces, node)
		if (sif->remove_dev)
			sif->remove_dev(dev, sif);
	mutex_unlock(&bus->p->mutex);

	if (klist_node_attached(&dev->p->knode_bus))
		klist_del(&dev->p->knode_bus);

	pr_debug("bus: '%s': remove device %s\n",
		 dev->bus->name, dev_name(dev));
	device_release_driver(dev);
	bus_put(dev->bus);
}

/**
 * bus_add_driver - Add a driver to the bus.
 * @drv: driver.
 */
int bus_add_driver(struct device_driver *drv)
{
	struct bus_type *bus;
	struct driver_private *priv;
	int error = 0;

	bus = bus_get(drv->bus);
	if (!bus)
		return -EINVAL;

	pr_debug("bus: '%s': add driver %s\n", bus->name, drv->name);

	priv = kzalloc(sizeof(*priv), 0);
	if (!priv) {
		error = -ENOMEM;
		goto out_put_bus;
	}
	klist_init(&priv->klist_devices, NULL, NULL);
	priv->driver = drv;
	drv->p = priv;
	klist_add_tail(&priv->knode_bus, &bus->p->klist_drivers);
	if (drv->bus->p->drivers_autoprobe) {
		error = driver_attach(drv);
		if (error)
			goto out_unregister;
	}
	return 0;

out_unregister:
	/* drv->p is freed in driver_release()  */
	drv->p = NULL;
out_put_bus:
	bus_put(bus);
	return error;
}

/**
 * bus_remove_driver - delete driver from bus's knowledge.
 * @drv: driver.
 *
 * Detach the driver from the devices it controls, and remove
 * it from its bus's list of drivers. Finally, we drop the reference
 * to the bus we took in bus_add_driver().
 */
void bus_remove_driver(struct device_driver *drv)
{
	if (!drv->bus)
		return;

	klist_remove(&drv->p->knode_bus);
	pr_debug("bus: '%s': remove driver %s\n", drv->bus->name, drv->name);
	driver_detach(drv);
	bus_put(drv->bus);
}

/* Helper for bus_rescan_devices's iter */
static int __must_check bus_rescan_devices_helper(struct device *dev,
						  void *data)
{
	int ret = 0;

	if (!dev->driver) {
		if (dev->parent && dev->bus->need_parent_lock)
			device_lock(dev->parent);
		ret = device_attach(dev);
		if (dev->parent && dev->bus->need_parent_lock)
			device_unlock(dev->parent);
	}
	return ret < 0 ? ret : 0;
}

/**
 * bus_rescan_devices - rescan devices on the bus for possible drivers
 * @bus: the bus to scan.
 *
 * This function will look for devices on the bus with no driver
 * attached and rescan it against existing drivers to see if it matches
 * any by calling device_attach() for the unbound devices.
 */
int bus_rescan_devices(struct bus_type *bus)
{
	return bus_for_each_dev(bus, NULL, NULL, bus_rescan_devices_helper);
}
EXPORT_SYMBOL_GPL(bus_rescan_devices);

/**
 * device_reprobe - remove driver for a device and probe for a new driver
 * @dev: the device to reprobe
 *
 * This function detaches the attached driver (if any) for the given
 * device and restarts the driver probing process.  It is intended
 * to use if probing criteria changed during a devices lifetime and
 * driver attachment should change accordingly.
 */
int device_reprobe(struct device *dev)
{
	if (dev->driver)
		device_driver_detach(dev);
	return bus_rescan_devices_helper(dev, NULL);
}
EXPORT_SYMBOL_GPL(device_reprobe);

static void klist_devices_get(struct klist_node *n)
{
	struct device_private *dev_prv = to_device_private_bus(n);
	struct device *dev = dev_prv->device;

	// get_device(dev);
}

static void klist_devices_put(struct klist_node *n)
{
	struct device_private *dev_prv = to_device_private_bus(n);
	struct device *dev = dev_prv->device;

	// put_device(dev);
}

/**
 * bus_register - register a driver-core subsystem
 * @bus: bus to register
 *
 * Once we have that, we register the bus with the kobject
 * infrastructure, then register the children subsystems it has:
 * the devices and drivers that belong to the subsystem.
 */
int bus_register(struct bus_type *bus)
{
	struct subsys_private *priv;
	struct lock_class_key *key = &bus->lock_key;

	priv = kzalloc(sizeof(struct subsys_private), 0);
	if (!priv)
		return -ENOMEM;

	priv->bus = bus;
	bus->p = priv;

	BLOCKING_INIT_NOTIFIER_HEAD(&priv->bus_notifier);

	priv->drivers_autoprobe = 1;

	INIT_LIST_HEAD(&priv->interfaces);
	__mutex_init(&priv->mutex, "subsys mutex", key);
	klist_init(&priv->klist_devices, klist_devices_get, klist_devices_put);
	klist_init(&priv->klist_drivers, NULL, NULL);

	pr_debug("bus: '%s': registered\n", bus->name);
	return 0;
}
EXPORT_SYMBOL_GPL(bus_register);

/**
 * bus_unregister - remove a bus from the system
 * @bus: bus.
 *
 * Unregister the child subsystems and the bus itself.
 * Finally, we call bus_put() to release the refcount
 */
void bus_unregister(struct bus_type *bus)
{
	pr_debug("bus: '%s': unregistering\n", bus->name);
	if (bus->dev_root)
		device_unregister(bus->dev_root);
}
EXPORT_SYMBOL_GPL(bus_unregister);

int bus_register_notifier(struct bus_type *bus, struct notifier_block *nb)
{
	return blocking_notifier_chain_register(&bus->p->bus_notifier, nb);
}
EXPORT_SYMBOL_GPL(bus_register_notifier);

int bus_unregister_notifier(struct bus_type *bus, struct notifier_block *nb)
{
	return blocking_notifier_chain_unregister(&bus->p->bus_notifier, nb);
}
EXPORT_SYMBOL_GPL(bus_unregister_notifier);

struct klist *bus_get_device_klist(struct bus_type *bus)
{
	return &bus->p->klist_devices;
}
EXPORT_SYMBOL_GPL(bus_get_device_klist);

/*
 * Yes, this forcibly breaks the klist abstraction temporarily.  It
 * just wants to sort the klist, not change reference counts and
 * take/drop locks rapidly in the process.  It does all this while
 * holding the lock for the list, so objects can't otherwise be
 * added/removed while we're swizzling.
 */
static void device_insertion_sort_klist(struct device *a, struct list_head *list,
					int (*compare)(const struct device *a,
							const struct device *b))
{
	struct klist_node *n;
	struct device_private *dev_prv;
	struct device *b;

	list_for_each_entry(n, list, n_node) {
		dev_prv = to_device_private_bus(n);
		b = dev_prv->device;
		if (compare(a, b) <= 0) {
			list_move_tail(&a->p->knode_bus.n_node,
				       &b->p->knode_bus.n_node);
			return;
		}
	}
	list_move_tail(&a->p->knode_bus.n_node, list);
}

void bus_sort_breadthfirst(struct bus_type *bus,
			   int (*compare)(const struct device *a,
					  const struct device *b))
{
	LIST_HEAD(sorted_devices);
	struct klist_node *n, *tmp;
	struct device_private *dev_prv;
	struct device *dev;
	struct klist *device_klist;

	device_klist = bus_get_device_klist(bus);

	spin_lock(&device_klist->k_lock);
	list_for_each_entry_safe(n, tmp, &device_klist->k_list, n_node) {
		dev_prv = to_device_private_bus(n);
		dev = dev_prv->device;
		device_insertion_sort_klist(dev, &sorted_devices, compare);
	}
	list_splice(&sorted_devices, &device_klist->k_list);
	spin_unlock(&device_klist->k_lock);
}
EXPORT_SYMBOL_GPL(bus_sort_breadthfirst);
