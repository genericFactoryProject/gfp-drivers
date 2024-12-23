// SPDX-License-Identifier: GPL-2.0
/*
 * drivers/base/core.c - core driver model code (device registration, etc)
 *
 * Copyright (c) 2002-3 Patrick Mochel
 * Copyright (c) 2002-3 Open Source Development Labs
 * Copyright (c) 2006 Greg Kroah-Hartman <gregkh@suse.de>
 * Copyright (c) 2006 Novell, Inc.
 */


#include <linux/device.h>
#include <linux/err.h>
#include <linux/fwnode.h>
#include <linux/init.h>
#include <linux/compat.h>
#include <linux/string.h>
#include <linux/kdev_t.h>
#include <linux/notifier.h>
#include <linux/of.h>
#include <linux/pm_runtime.h>
#include <linux/kstrtox.h>

#include "base.h"
#include "power/power.h"

/* Links */

/* Device links support. */
static LIST_HEAD(deferred_sync);
static unsigned int defer_sync_state_count = 1;

static DEFINE_MUTEX(fwnode_link_lock);
static bool fw_devlink_is_permissive(void);
static bool fw_devlink_drv_reg_done;

/**
 * fwnode_link_add - Create a link between two fwnode_handles.
 * @con: Consumer end of the link.
 * @sup: Supplier end of the link.
 *
 * Create a fwnode link between fwnode handles @con and @sup. The fwnode link
 * represents the detail that the firmware lists @sup fwnode as supplying a
 * resource to @con.
 *
 * The driver core will use the fwnode link to create a device link between the
 * two device objects corresponding to @con and @sup when they are created. The
 * driver core will automatically delete the fwnode link between @con and @sup
 * after doing that.
 *
 * Attempts to create duplicate links between the same pair of fwnode handles
 * are ignored and there is no reference counting.
 */
int fwnode_link_add(struct fwnode_handle *con, struct fwnode_handle *sup)
{
	struct fwnode_link *link;
	int ret = 0;

	mutex_lock(&fwnode_link_lock);

	list_for_each_entry(link, &sup->consumers, s_hook)
		if (link->consumer == con)
			goto out;

	link = kzalloc(sizeof(*link), 0);
	if (!link) {
		ret = -ENOMEM;
		goto out;
	}

	link->supplier = sup;
	INIT_LIST_HEAD(&link->s_hook);
	link->consumer = con;
	INIT_LIST_HEAD(&link->c_hook);

	list_add(&link->s_hook, &sup->consumers);
	list_add(&link->c_hook, &con->suppliers);
	pr_debug("%pfwP Linked as a fwnode consumer to %pfwP\n",
		 con, sup);
out:
	mutex_unlock(&fwnode_link_lock);

	return ret;
}

/**
 * __fwnode_link_del - Delete a link between two fwnode_handles.
 * @link: the fwnode_link to be deleted
 *
 * The fwnode_link_lock needs to be held when this function is called.
 */
static void __fwnode_link_del(struct fwnode_link *link)
{
	pr_debug("%pfwP Dropping the fwnode link to %pfwP\n",
		 link->consumer, link->supplier);
	list_del(&link->s_hook);
	list_del(&link->c_hook);
	kfree(link);
}

/**
 * fwnode_links_purge_suppliers - Delete all supplier links of fwnode_handle.
 * @fwnode: fwnode whose supplier links need to be deleted
 *
 * Deletes all supplier links connecting directly to @fwnode.
 */
static void fwnode_links_purge_suppliers(struct fwnode_handle *fwnode)
{
	struct fwnode_link *link, *tmp;

	mutex_lock(&fwnode_link_lock);
	list_for_each_entry_safe(link, tmp, &fwnode->suppliers, c_hook)
		__fwnode_link_del(link);
	mutex_unlock(&fwnode_link_lock);
}

/**
 * fwnode_links_purge_consumers - Delete all consumer links of fwnode_handle.
 * @fwnode: fwnode whose consumer links need to be deleted
 *
 * Deletes all consumer links connecting directly to @fwnode.
 */
static void fwnode_links_purge_consumers(struct fwnode_handle *fwnode)
{
	struct fwnode_link *link, *tmp;

	mutex_lock(&fwnode_link_lock);
	list_for_each_entry_safe(link, tmp, &fwnode->consumers, s_hook)
		__fwnode_link_del(link);
	mutex_unlock(&fwnode_link_lock);
}

/**
 * fwnode_links_purge - Delete all links connected to a fwnode_handle.
 * @fwnode: fwnode whose links needs to be deleted
 *
 * Deletes all links connecting directly to a fwnode.
 */
void fwnode_links_purge(struct fwnode_handle *fwnode)
{
	fwnode_links_purge_suppliers(fwnode);
	fwnode_links_purge_consumers(fwnode);
}

void fw_devlink_purge_absent_suppliers(struct fwnode_handle *fwnode)
{
	struct fwnode_handle *child;

	/* Don't purge consumer links of an added child */
	if (fwnode->dev)
		return;

	fwnode->flags |= FWNODE_FLAG_NOT_DEVICE;
	fwnode_links_purge_consumers(fwnode);

	fwnode_for_each_available_child_node(fwnode, child)
		fw_devlink_purge_absent_suppliers(child);
}
EXPORT_SYMBOL_GPL(fw_devlink_purge_absent_suppliers);


static DECLARE_RWSEM(device_links_lock);

static inline void device_links_write_lock(void)
{
	down_write(&device_links_lock);
}

static inline void device_links_write_unlock(void)
{
	up_write(&device_links_lock);
}

int device_links_read_lock(void)
{
	down_read(&device_links_lock);
	return 0;
}

void device_links_read_unlock(int not_used)
{
	up_read(&device_links_lock);
}

#ifdef CONFIG_DEBUG_LOCK_ALLOC
int device_links_read_lock_held(void)
{
	return lockdep_is_held(&device_links_lock);
}
#endif

static inline void device_link_synchronize_removal(void)
{
}

static void device_link_remove_from_lists(struct device_link *link)
{
	list_del(&link->s_node);
	list_del(&link->c_node);
}


static bool device_is_ancestor(struct device *dev, struct device *target)
{
	while (target->parent) {
		target = target->parent;
		if (dev == target)
			return true;
	}
	return false;
}

/**
 * device_is_dependent - Check if one device depends on another one
 * @dev: Device to check dependencies for.
 * @target: Device to check against.
 *
 * Check if @target depends on @dev or any device dependent on it (its child or
 * its consumer etc).  Return 1 if that is the case or 0 otherwise.
 */
int device_is_dependent(struct device *dev, void *target)
{
	struct device_link *link;
	int ret;

	/*
	 * The "ancestors" check is needed to catch the case when the target
	 * device has not been completely initialized yet and it is still
	 * missing from the list of children of its parent device.
	 */
	if (dev == target || device_is_ancestor(dev, target))
		return 1;

	ret = device_for_each_child(dev, target, device_is_dependent);
	if (ret)
		return ret;

	list_for_each_entry(link, &dev->links.consumers, s_node) {
		if ((link->flags & ~DL_FLAG_INFERRED) ==
		    (DL_FLAG_SYNC_STATE_ONLY | DL_FLAG_MANAGED))
			continue;

		if (link->consumer == target)
			return 1;

		ret = device_is_dependent(link->consumer, target);
		if (ret)
			break;
	}
	return ret;
}

static void device_link_init_status(struct device_link *link,
				    struct device *consumer,
				    struct device *supplier)
{
	switch (supplier->links.status) {
	case DL_DEV_PROBING:
		switch (consumer->links.status) {
		case DL_DEV_PROBING:
			/*
			 * A consumer driver can create a link to a supplier
			 * that has not completed its probing yet as long as it
			 * knows that the supplier is already functional (for
			 * example, it has just acquired some resources from the
			 * supplier).
			 */
			link->status = DL_STATE_CONSUMER_PROBE;
			break;
		default:
			link->status = DL_STATE_DORMANT;
			break;
		}
		break;
	case DL_DEV_DRIVER_BOUND:
		switch (consumer->links.status) {
		case DL_DEV_PROBING:
			link->status = DL_STATE_CONSUMER_PROBE;
			break;
		case DL_DEV_DRIVER_BOUND:
			link->status = DL_STATE_ACTIVE;
			break;
		default:
			link->status = DL_STATE_AVAILABLE;
			break;
		}
		break;
	case DL_DEV_UNBINDING:
		link->status = DL_STATE_SUPPLIER_UNBIND;
		break;
	default:
		link->status = DL_STATE_DORMANT;
		break;
	}
}

static int device_reorder_to_tail(struct device *dev, void *not_used)
{
	struct device_link *link;

	if (device_pm_initialized(dev))
		device_pm_move_last(dev);

	device_for_each_child(dev, NULL, device_reorder_to_tail);
	list_for_each_entry(link, &dev->links.consumers, s_node) {
		if ((link->flags & ~DL_FLAG_INFERRED) ==
		    (DL_FLAG_SYNC_STATE_ONLY | DL_FLAG_MANAGED))
			continue;
		device_reorder_to_tail(link->consumer, NULL);
	}

	return 0;
}

/**
 * device_pm_move_to_tail - Move set of devices to the end of device lists
 * @dev: Device to move
 *
 * This is a device_reorder_to_tail() wrapper taking the requisite locks.
 *
 * It moves the @dev along with all of its children and all of its consumers
 * to the ends of the device_kset and dpm_list, recursively.
 */
void device_pm_move_to_tail(struct device *dev)
{
	int idx;

	idx = device_links_read_lock();
	device_pm_lock();
	device_reorder_to_tail(dev, NULL);
	device_pm_unlock();
	device_links_read_unlock(idx);
}

#define to_devlink(dev)	container_of((dev), struct device_link, link_dev)

#if 0
static void device_link_release_fn(struct work_struct *work)
{
	struct device_link *link = container_of(work, struct device_link, rm_work);

	/* Ensure that all references to the link object have been dropped. */
	device_link_synchronize_removal();

	pm_runtime_release_supplier(link, true);

	put_device(link->consumer);
	put_device(link->supplier);
	kfree(link);
}
#endif

static void devlink_dev_release(struct device *dev)
{

}

static struct class devlink_class = {
	.name = "devlink",
	.dev_release = devlink_dev_release,
};

static struct class_interface devlink_class_intf = {
	.class = &devlink_class,
};

static int __init devlink_class_init(void)
{
	int ret;

	ret = class_register(&devlink_class);
	if (ret)
		return ret;

	ret = class_interface_register(&devlink_class_intf);
	if (ret)
		class_unregister(&devlink_class);

	return ret;
}
postcore_initcall(devlink_class_init);

#define DL_MANAGED_LINK_FLAGS (DL_FLAG_AUTOREMOVE_CONSUMER | \
			       DL_FLAG_AUTOREMOVE_SUPPLIER | \
			       DL_FLAG_AUTOPROBE_CONSUMER  | \
			       DL_FLAG_SYNC_STATE_ONLY | \
			       DL_FLAG_INFERRED)

#define DL_ADD_VALID_FLAGS (DL_MANAGED_LINK_FLAGS | DL_FLAG_STATELESS | \
			    DL_FLAG_PM_RUNTIME | DL_FLAG_RPM_ACTIVE)

/**
 * device_link_add - Create a link between two devices.
 * @consumer: Consumer end of the link.
 * @supplier: Supplier end of the link.
 * @flags: Link flags.
 *
 * The caller is responsible for the proper synchronization of the link creation
 * with runtime PM.  First, setting the DL_FLAG_PM_RUNTIME flag will cause the
 * runtime PM framework to take the link into account.  Second, if the
 * DL_FLAG_RPM_ACTIVE flag is set in addition to it, the supplier devices will
 * be forced into the active meta state and reference-counted upon the creation
 * of the link.  If DL_FLAG_PM_RUNTIME is not set, DL_FLAG_RPM_ACTIVE will be
 * ignored.
 *
 * If DL_FLAG_STATELESS is set in @flags, the caller of this function is
 * expected to release the link returned by it directly with the help of either
 * device_link_del() or device_link_remove().
 *
 * If that flag is not set, however, the caller of this function is handing the
 * management of the link over to the driver core entirely and its return value
 * can only be used to check whether or not the link is present.  In that case,
 * the DL_FLAG_AUTOREMOVE_CONSUMER and DL_FLAG_AUTOREMOVE_SUPPLIER device link
 * flags can be used to indicate to the driver core when the link can be safely
 * deleted.  Namely, setting one of them in @flags indicates to the driver core
 * that the link is not going to be used (by the given caller of this function)
 * after unbinding the consumer or supplier driver, respectively, from its
 * device, so the link can be deleted at that point.  If none of them is set,
 * the link will be maintained until one of the devices pointed to by it (either
 * the consumer or the supplier) is unregistered.
 *
 * Also, if DL_FLAG_STATELESS, DL_FLAG_AUTOREMOVE_CONSUMER and
 * DL_FLAG_AUTOREMOVE_SUPPLIER are not set in @flags (that is, a persistent
 * managed device link is being added), the DL_FLAG_AUTOPROBE_CONSUMER flag can
 * be used to request the driver core to automatically probe for a consumer
 * driver after successfully binding a driver to the supplier device.
 *
 * The combination of DL_FLAG_STATELESS and one of DL_FLAG_AUTOREMOVE_CONSUMER,
 * DL_FLAG_AUTOREMOVE_SUPPLIER, or DL_FLAG_AUTOPROBE_CONSUMER set in @flags at
 * the same time is invalid and will cause NULL to be returned upfront.
 * However, if a device link between the given @consumer and @supplier pair
 * exists already when this function is called for them, the existing link will
 * be returned regardless of its current type and status (the link's flags may
 * be modified then).  The caller of this function is then expected to treat
 * the link as though it has just been created, so (in particular) if
 * DL_FLAG_STATELESS was passed in @flags, the link needs to be released
 * explicitly when not needed any more (as stated above).
 *
 * A side effect of the link creation is re-ordering of dpm_list and the
 * devices_kset list by moving the consumer device and all devices depending
 * on it to the ends of these lists (that does not happen to devices that have
 * not been registered when this function is called).
 *
 * The supplier device is required to be registered when this function is called
 * and NULL will be returned if that is not the case.  The consumer device need
 * not be registered, however.
 */
struct device_link *device_link_add(struct device *consumer,
				    struct device *supplier, u32 flags)
{
	struct device_link *link;

	if (!consumer || !supplier || consumer == supplier ||
	    flags & ~DL_ADD_VALID_FLAGS ||
	    (flags & DL_FLAG_STATELESS && flags & DL_MANAGED_LINK_FLAGS) ||
	    (flags & DL_FLAG_SYNC_STATE_ONLY &&
	     (flags & ~DL_FLAG_INFERRED) != DL_FLAG_SYNC_STATE_ONLY) ||
	    (flags & DL_FLAG_AUTOPROBE_CONSUMER &&
	     flags & (DL_FLAG_AUTOREMOVE_CONSUMER |
		      DL_FLAG_AUTOREMOVE_SUPPLIER)))
		return NULL;

	if (flags & DL_FLAG_PM_RUNTIME && flags & DL_FLAG_RPM_ACTIVE) {
		if (pm_runtime_get_sync(supplier) < 0) {
			pm_runtime_put_noidle(supplier);
			return NULL;
		}
	}

	if (!(flags & DL_FLAG_STATELESS))
		flags |= DL_FLAG_MANAGED;

	device_links_write_lock();
	device_pm_lock();

	/*
	 * If the supplier has not been fully registered yet or there is a
	 * reverse (non-SYNC_STATE_ONLY) dependency between the consumer and
	 * the supplier already in the graph, return NULL. If the link is a
	 * SYNC_STATE_ONLY link, we don't check for reverse dependencies
	 * because it only affects sync_state() callbacks.
	 */
	if (!device_pm_initialized(supplier)
	    || (!(flags & DL_FLAG_SYNC_STATE_ONLY) &&
		  device_is_dependent(consumer, supplier))) {
		link = NULL;
		goto out;
	}

	/*
	 * SYNC_STATE_ONLY links are useless once a consumer device has probed.
	 * So, only create it if the consumer hasn't probed yet.
	 */
	if (flags & DL_FLAG_SYNC_STATE_ONLY &&
	    consumer->links.status != DL_DEV_NO_DRIVER &&
	    consumer->links.status != DL_DEV_PROBING) {
		link = NULL;
		goto out;
	}

	/*
	 * DL_FLAG_AUTOREMOVE_SUPPLIER indicates that the link will be needed
	 * longer than for DL_FLAG_AUTOREMOVE_CONSUMER and setting them both
	 * together doesn't make sense, so prefer DL_FLAG_AUTOREMOVE_SUPPLIER.
	 */
	if (flags & DL_FLAG_AUTOREMOVE_SUPPLIER)
		flags &= ~DL_FLAG_AUTOREMOVE_CONSUMER;

	list_for_each_entry(link, &supplier->links.consumers, s_node) {
		if (link->consumer != consumer)
			continue;

		if (link->flags & DL_FLAG_INFERRED &&
		    !(flags & DL_FLAG_INFERRED))
			link->flags &= ~DL_FLAG_INFERRED;

		if (flags & DL_FLAG_PM_RUNTIME) {
			if (!(link->flags & DL_FLAG_PM_RUNTIME)) {
				pm_runtime_new_link(consumer);
				link->flags |= DL_FLAG_PM_RUNTIME;
			}
			if (flags & DL_FLAG_RPM_ACTIVE)
				refcount_inc(&link->rpm_active);
		}

		if (flags & DL_FLAG_STATELESS) {
			kref_get(&link->kref);
			if (link->flags & DL_FLAG_SYNC_STATE_ONLY &&
			    !(link->flags & DL_FLAG_STATELESS)) {
				link->flags |= DL_FLAG_STATELESS;
				goto reorder;
			} else {
				link->flags |= DL_FLAG_STATELESS;
				goto out;
			}
		}

		/*
		 * If the life time of the link following from the new flags is
		 * longer than indicated by the flags of the existing link,
		 * update the existing link to stay around longer.
		 */
		if (flags & DL_FLAG_AUTOREMOVE_SUPPLIER) {
			if (link->flags & DL_FLAG_AUTOREMOVE_CONSUMER) {
				link->flags &= ~DL_FLAG_AUTOREMOVE_CONSUMER;
				link->flags |= DL_FLAG_AUTOREMOVE_SUPPLIER;
			}
		} else if (!(flags & DL_FLAG_AUTOREMOVE_CONSUMER)) {
			link->flags &= ~(DL_FLAG_AUTOREMOVE_CONSUMER |
					 DL_FLAG_AUTOREMOVE_SUPPLIER);
		}
		if (!(link->flags & DL_FLAG_MANAGED)) {
			kref_get(&link->kref);
			link->flags |= DL_FLAG_MANAGED;
			device_link_init_status(link, consumer, supplier);
		}
		if (link->flags & DL_FLAG_SYNC_STATE_ONLY &&
		    !(flags & DL_FLAG_SYNC_STATE_ONLY)) {
			link->flags &= ~DL_FLAG_SYNC_STATE_ONLY;
			goto reorder;
		}

		goto out;
	}

	link = kzalloc(sizeof(*link), 0);
	if (!link)
		goto out;

	refcount_set(&link->rpm_active, 1);

	get_device(supplier);
	link->supplier = supplier;
	INIT_LIST_HEAD(&link->s_node);
	get_device(consumer);
	link->consumer = consumer;
	INIT_LIST_HEAD(&link->c_node);
	link->flags = flags;
	kref_init(&link->kref);

	link->link_dev.class = &devlink_class;
	device_set_pm_not_required(&link->link_dev);
	dev_set_name(&link->link_dev, "%s:%s--%s:%s",
		     dev_bus_name(supplier), dev_name(supplier),
		     dev_bus_name(consumer), dev_name(consumer));
	if (device_register(&link->link_dev)) {
		put_device(&link->link_dev);
		link = NULL;
		goto out;
	}

	if (flags & DL_FLAG_PM_RUNTIME) {
		if (flags & DL_FLAG_RPM_ACTIVE)
			refcount_inc(&link->rpm_active);

		pm_runtime_new_link(consumer);
	}

	/* Determine the initial link state. */
	if (flags & DL_FLAG_STATELESS)
		link->status = DL_STATE_NONE;
	else
		device_link_init_status(link, consumer, supplier);

	/*
	 * Some callers expect the link creation during consumer driver probe to
	 * resume the supplier even without DL_FLAG_RPM_ACTIVE.
	 */
	if (link->status == DL_STATE_CONSUMER_PROBE &&
	    flags & DL_FLAG_PM_RUNTIME)
		pm_runtime_resume(supplier);

	list_add_tail(&link->s_node, &supplier->links.consumers);
	list_add_tail(&link->c_node, &consumer->links.suppliers);

	if (flags & DL_FLAG_SYNC_STATE_ONLY) {
		dev_dbg(consumer,
			"Linked as a sync state only consumer to %s\n",
			dev_name(supplier));
		goto out;
	}

reorder:
	/*
	 * Move the consumer and all of the devices depending on it to the end
	 * of dpm_list and the devices_kset list.
	 *
	 * It is necessary to hold dpm_list locked throughout all that or else
	 * we may end up suspending with a wrong ordering of it.
	 */
	device_reorder_to_tail(consumer, NULL);

	dev_dbg(consumer, "Linked as a consumer to %s\n", dev_name(supplier));

out:
	device_pm_unlock();
	device_links_write_unlock();

	if ((flags & DL_FLAG_PM_RUNTIME && flags & DL_FLAG_RPM_ACTIVE) && !link)
		pm_runtime_put(supplier);

	return link;
}
EXPORT_SYMBOL_GPL(device_link_add);

static void __device_link_del(struct kref *kref)
{
	struct device_link *link = container_of(kref, struct device_link, kref);

	dev_dbg(link->consumer, "Dropping the link to %s\n",
		dev_name(link->supplier));

	pm_runtime_drop_link(link);

	device_link_remove_from_lists(link);
	device_unregister(&link->link_dev);
}

static void device_link_put_kref(struct device_link *link)
{
	if (link->flags & DL_FLAG_STATELESS)
		kref_put(&link->kref, __device_link_del);
	else if (!device_is_registered(link->consumer))
		__device_link_del(&link->kref);
	else
		WARN(1, "Unable to drop a managed device link reference\n");
}

/**
 * device_link_del - Delete a stateless link between two devices.
 * @link: Device link to delete.
 *
 * The caller must ensure proper synchronization of this function with runtime
 * PM.  If the link was added multiple times, it needs to be deleted as often.
 * Care is required for hotplugged devices:  Their links are purged on removal
 * and calling device_link_del() is then no longer allowed.
 */
void device_link_del(struct device_link *link)
{
	device_links_write_lock();
	device_link_put_kref(link);
	device_links_write_unlock();
}
EXPORT_SYMBOL_GPL(device_link_del);

/**
 * device_link_remove - Delete a stateless link between two devices.
 * @consumer: Consumer end of the link.
 * @supplier: Supplier end of the link.
 *
 * The caller must ensure proper synchronization of this function with runtime
 * PM.
 */
void device_link_remove(void *consumer, struct device *supplier)
{
	struct device_link *link;

	if (WARN_ON(consumer == supplier))
		return;

	device_links_write_lock();

	list_for_each_entry(link, &supplier->links.consumers, s_node) {
		if (link->consumer == consumer) {
			device_link_put_kref(link);
			break;
		}
	}

	device_links_write_unlock();
}
EXPORT_SYMBOL_GPL(device_link_remove);

static void device_links_missing_supplier(struct device *dev)
{
	struct device_link *link;

	list_for_each_entry(link, &dev->links.suppliers, c_node) {
		if (link->status != DL_STATE_CONSUMER_PROBE)
			continue;

		if (link->supplier->links.status == DL_DEV_DRIVER_BOUND) {
			WRITE_ONCE(link->status, DL_STATE_AVAILABLE);
		} else {
			WARN_ON(!(link->flags & DL_FLAG_SYNC_STATE_ONLY));
			WRITE_ONCE(link->status, DL_STATE_DORMANT);
		}
	}
}

/**
 * device_links_check_suppliers - Check presence of supplier drivers.
 * @dev: Consumer device.
 *
 * Check links from this device to any suppliers.  Walk the list of the device's
 * links to suppliers and see if all of them are available.  If not, simply
 * return -EPROBE_DEFER.
 *
 * We need to guarantee that the supplier will not go away after the check has
 * been positive here.  It only can go away in __device_release_driver() and
 * that function  checks the device's links to consumers.  This means we need to
 * mark the link as "consumer probe in progress" to make the supplier removal
 * wait for us to complete (or bad things may happen).
 *
 * Links without the DL_FLAG_MANAGED flag set are ignored.
 */
int device_links_check_suppliers(struct device *dev)
{
	struct device_link *link;
	int ret = 0;
	struct fwnode_handle *sup_fw;

	/*
	 * Device waiting for supplier to become available is not allowed to
	 * probe.
	 */
	mutex_lock(&fwnode_link_lock);
	if (dev->fwnode && !list_empty(&dev->fwnode->suppliers) &&
	    !fw_devlink_is_permissive()) {
		sup_fw = list_first_entry(&dev->fwnode->suppliers,
					  struct fwnode_link,
					  c_hook)->supplier;
		dev_err_probe(dev, -EPROBE_DEFER, "wait for supplier %pfwP\n",
			      sup_fw);
		mutex_unlock(&fwnode_link_lock);
		return -EPROBE_DEFER;
	}
	mutex_unlock(&fwnode_link_lock);

	device_links_write_lock();

	list_for_each_entry(link, &dev->links.suppliers, c_node) {
		if (!(link->flags & DL_FLAG_MANAGED))
			continue;

		if (link->status != DL_STATE_AVAILABLE &&
		    !(link->flags & DL_FLAG_SYNC_STATE_ONLY)) {
			device_links_missing_supplier(dev);
			dev_err_probe(dev, -EPROBE_DEFER,
				      "supplier %s not ready\n",
				      dev_name(link->supplier));
			ret = -EPROBE_DEFER;
			break;
		}
		WRITE_ONCE(link->status, DL_STATE_CONSUMER_PROBE);
	}
	dev->links.status = DL_DEV_PROBING;

	device_links_write_unlock();
	return ret;
}

/**
 * __device_links_queue_sync_state - Queue a device for sync_state() callback
 * @dev: Device to call sync_state() on
 * @list: List head to queue the @dev on
 *
 * Queues a device for a sync_state() callback when the device links write lock
 * isn't held. This allows the sync_state() execution flow to use device links
 * APIs.  The caller must ensure this function is called with
 * device_links_write_lock() held.
 *
 * This function does a get_device() to make sure the device is not freed while
 * on this list.
 *
 * So the caller must also ensure that device_links_flush_sync_list() is called
 * as soon as the caller releases device_links_write_lock().  This is necessary
 * to make sure the sync_state() is called in a timely fashion and the
 * put_device() is called on this device.
 */
static void __device_links_queue_sync_state(struct device *dev,
					    struct list_head *list)
{
	struct device_link *link;

	if (!dev_has_sync_state(dev))
		return;
	if (dev->state_synced)
		return;

	list_for_each_entry(link, &dev->links.consumers, s_node) {
		if (!(link->flags & DL_FLAG_MANAGED))
			continue;
		if (link->status != DL_STATE_ACTIVE)
			return;
	}

	/*
	 * Set the flag here to avoid adding the same device to a list more
	 * than once. This can happen if new consumers get added to the device
	 * and probed before the list is flushed.
	 */
	dev->state_synced = true;

	if (WARN_ON(!list_empty(&dev->links.defer_sync)))
		return;

	get_device(dev);
	list_add_tail(&dev->links.defer_sync, list);
}

/**
 * device_links_flush_sync_list - Call sync_state() on a list of devices
 * @list: List of devices to call sync_state() on
 * @dont_lock_dev: Device for which lock is already held by the caller
 *
 * Calls sync_state() on all the devices that have been queued for it. This
 * function is used in conjunction with __device_links_queue_sync_state(). The
 * @dont_lock_dev parameter is useful when this function is called from a
 * context where a device lock is already held.
 */
static void device_links_flush_sync_list(struct list_head *list,
					 struct device *dont_lock_dev)
{
	struct device *dev, *tmp;

	list_for_each_entry_safe(dev, tmp, list, links.defer_sync) {
		list_del_init(&dev->links.defer_sync);

		if (dev != dont_lock_dev)
			device_lock(dev);

		if (dev->bus->sync_state)
			dev->bus->sync_state(dev);
		else if (dev->driver && dev->driver->sync_state)
			dev->driver->sync_state(dev);

		if (dev != dont_lock_dev)
			device_unlock(dev);

		put_device(dev);
	}
}

void device_links_supplier_sync_state_pause(void)
{
	device_links_write_lock();
	defer_sync_state_count++;
	device_links_write_unlock();
}

void device_links_supplier_sync_state_resume(void)
{
	struct device *dev, *tmp;
	LIST_HEAD(sync_list);

	device_links_write_lock();
	if (!defer_sync_state_count) {
		WARN(true, "Unmatched sync_state pause/resume!");
		goto out;
	}
	defer_sync_state_count--;
	if (defer_sync_state_count)
		goto out;

	list_for_each_entry_safe(dev, tmp, &deferred_sync, links.defer_sync) {
		/*
		 * Delete from deferred_sync list before queuing it to
		 * sync_list because defer_sync is used for both lists.
		 */
		list_del_init(&dev->links.defer_sync);
		__device_links_queue_sync_state(dev, &sync_list);
	}
out:
	device_links_write_unlock();

	device_links_flush_sync_list(&sync_list, NULL);
}

static int sync_state_resume_initcall(void)
{
	device_links_supplier_sync_state_resume();
	return 0;
}
late_initcall(sync_state_resume_initcall);

static void __device_links_supplier_defer_sync(struct device *sup)
{
	if (list_empty(&sup->links.defer_sync) && dev_has_sync_state(sup))
		list_add_tail(&sup->links.defer_sync, &deferred_sync);
}

static void device_link_drop_managed(struct device_link *link)
{
	link->flags &= ~DL_FLAG_MANAGED;
	WRITE_ONCE(link->status, DL_STATE_NONE);
	kref_put(&link->kref, __device_link_del);
}

/**
 * device_links_force_bind - Prepares device to be force bound
 * @dev: Consumer device.
 *
 * device_bind_driver() force binds a device to a driver without calling any
 * driver probe functions. So the consumer really isn't going to wait for any
 * supplier before it's bound to the driver. We still want the device link
 * states to be sensible when this happens.
 *
 * In preparation for device_bind_driver(), this function goes through each
 * supplier device links and checks if the supplier is bound. If it is, then
 * the device link status is set to CONSUMER_PROBE. Otherwise, the device link
 * is dropped. Links without the DL_FLAG_MANAGED flag set are ignored.
 */
void device_links_force_bind(struct device *dev)
{
	struct device_link *link, *ln;

	device_links_write_lock();

	list_for_each_entry_safe(link, ln, &dev->links.suppliers, c_node) {
		if (!(link->flags & DL_FLAG_MANAGED))
			continue;

		if (link->status != DL_STATE_AVAILABLE) {
			device_link_drop_managed(link);
			continue;
		}
		WRITE_ONCE(link->status, DL_STATE_CONSUMER_PROBE);
	}
	dev->links.status = DL_DEV_PROBING;

	device_links_write_unlock();
}

/**
 * device_links_driver_bound - Update device links after probing its driver.
 * @dev: Device to update the links for.
 *
 * The probe has been successful, so update links from this device to any
 * consumers by changing their status to "available".
 *
 * Also change the status of @dev's links to suppliers to "active".
 *
 * Links without the DL_FLAG_MANAGED flag set are ignored.
 */
void device_links_driver_bound(struct device *dev)
{
	struct device_link *link, *ln;
	LIST_HEAD(sync_list);

	/*
	 * If a device binds successfully, it's expected to have created all
	 * the device links it needs to or make new device links as it needs
	 * them. So, fw_devlink no longer needs to create device links to any
	 * of the device's suppliers.
	 *
	 * Also, if a child firmware node of this bound device is not added as
	 * a device by now, assume it is never going to be added and make sure
	 * other devices don't defer probe indefinitely by waiting for such a
	 * child device.
	 */
	if (dev->fwnode && dev->fwnode->dev == dev) {
		struct fwnode_handle *child;
		fwnode_links_purge_suppliers(dev->fwnode);
		fwnode_for_each_available_child_node(dev->fwnode, child)
			fw_devlink_purge_absent_suppliers(child);
	}

	device_links_write_lock();

	list_for_each_entry(link, &dev->links.consumers, s_node) {
		if (!(link->flags & DL_FLAG_MANAGED))
			continue;

		/*
		 * Links created during consumer probe may be in the "consumer
		 * probe" state to start with if the supplier is still probing
		 * when they are created and they may become "active" if the
		 * consumer probe returns first.  Skip them here.
		 */
		if (link->status == DL_STATE_CONSUMER_PROBE ||
		    link->status == DL_STATE_ACTIVE)
			continue;

		WARN_ON(link->status != DL_STATE_DORMANT);
		WRITE_ONCE(link->status, DL_STATE_AVAILABLE);

		//if (link->flags & DL_FLAG_AUTOPROBE_CONSUMER)
		//	driver_deferred_probe_add(link->consumer);
	}

	if (defer_sync_state_count)
		__device_links_supplier_defer_sync(dev);
	else
		__device_links_queue_sync_state(dev, &sync_list);

	list_for_each_entry_safe(link, ln, &dev->links.suppliers, c_node) {
		struct device *supplier;

		if (!(link->flags & DL_FLAG_MANAGED))
			continue;

		supplier = link->supplier;
		if (link->flags & DL_FLAG_SYNC_STATE_ONLY) {
			/*
			 * When DL_FLAG_SYNC_STATE_ONLY is set, it means no
			 * other DL_MANAGED_LINK_FLAGS have been set. So, it's
			 * save to drop the managed link completely.
			 */
			device_link_drop_managed(link);
		} else {
			WARN_ON(link->status != DL_STATE_CONSUMER_PROBE);
			WRITE_ONCE(link->status, DL_STATE_ACTIVE);
		}

		/*
		 * This needs to be done even for the deleted
		 * DL_FLAG_SYNC_STATE_ONLY device link in case it was the last
		 * device link that was preventing the supplier from getting a
		 * sync_state() call.
		 */
		if (defer_sync_state_count)
			__device_links_supplier_defer_sync(supplier);
		else
			__device_links_queue_sync_state(supplier, &sync_list);
	}

	dev->links.status = DL_DEV_DRIVER_BOUND;

	device_links_write_unlock();

	device_links_flush_sync_list(&sync_list, dev);
}

/**
 * __device_links_no_driver - Update links of a device without a driver.
 * @dev: Device without a drvier.
 *
 * Delete all non-persistent links from this device to any suppliers.
 *
 * Persistent links stay around, but their status is changed to "available",
 * unless they already are in the "supplier unbind in progress" state in which
 * case they need not be updated.
 *
 * Links without the DL_FLAG_MANAGED flag set are ignored.
 */
static void __device_links_no_driver(struct device *dev)
{
	struct device_link *link, *ln;

	list_for_each_entry_safe_reverse(link, ln, &dev->links.suppliers, c_node) {
		if (!(link->flags & DL_FLAG_MANAGED))
			continue;

		if (link->flags & DL_FLAG_AUTOREMOVE_CONSUMER) {
			device_link_drop_managed(link);
			continue;
		}

		if (link->status != DL_STATE_CONSUMER_PROBE &&
		    link->status != DL_STATE_ACTIVE)
			continue;

		if (link->supplier->links.status == DL_DEV_DRIVER_BOUND) {
			WRITE_ONCE(link->status, DL_STATE_AVAILABLE);
		} else {
			WARN_ON(!(link->flags & DL_FLAG_SYNC_STATE_ONLY));
			WRITE_ONCE(link->status, DL_STATE_DORMANT);
		}
	}

	dev->links.status = DL_DEV_NO_DRIVER;
}

/**
 * device_links_no_driver - Update links after failing driver probe.
 * @dev: Device whose driver has just failed to probe.
 *
 * Clean up leftover links to consumers for @dev and invoke
 * %__device_links_no_driver() to update links to suppliers for it as
 * appropriate.
 *
 * Links without the DL_FLAG_MANAGED flag set are ignored.
 */
void device_links_no_driver(struct device *dev)
{
	struct device_link *link;

	device_links_write_lock();

	list_for_each_entry(link, &dev->links.consumers, s_node) {
		if (!(link->flags & DL_FLAG_MANAGED))
			continue;

		/*
		 * The probe has failed, so if the status of the link is
		 * "consumer probe" or "active", it must have been added by
		 * a probing consumer while this device was still probing.
		 * Change its state to "dormant", as it represents a valid
		 * relationship, but it is not functionally meaningful.
		 */
		if (link->status == DL_STATE_CONSUMER_PROBE ||
		    link->status == DL_STATE_ACTIVE)
			WRITE_ONCE(link->status, DL_STATE_DORMANT);
	}

	__device_links_no_driver(dev);

	device_links_write_unlock();
}

/**
 * device_links_driver_cleanup - Update links after driver removal.
 * @dev: Device whose driver has just gone away.
 *
 * Update links to consumers for @dev by changing their status to "dormant" and
 * invoke %__device_links_no_driver() to update links to suppliers for it as
 * appropriate.
 *
 * Links without the DL_FLAG_MANAGED flag set are ignored.
 */
void device_links_driver_cleanup(struct device *dev)
{
	struct device_link *link, *ln;

	device_links_write_lock();

	list_for_each_entry_safe(link, ln, &dev->links.consumers, s_node) {
		if (!(link->flags & DL_FLAG_MANAGED))
			continue;

		WARN_ON(link->flags & DL_FLAG_AUTOREMOVE_CONSUMER);
		WARN_ON(link->status != DL_STATE_SUPPLIER_UNBIND);

		/*
		 * autoremove the links between this @dev and its consumer
		 * devices that are not active, i.e. where the link state
		 * has moved to DL_STATE_SUPPLIER_UNBIND.
		 */
		if (link->status == DL_STATE_SUPPLIER_UNBIND &&
		    link->flags & DL_FLAG_AUTOREMOVE_SUPPLIER)
			device_link_drop_managed(link);

		WRITE_ONCE(link->status, DL_STATE_DORMANT);
	}

	list_del_init(&dev->links.defer_sync);
	__device_links_no_driver(dev);

	device_links_write_unlock();
}

/**
 * device_links_busy - Check if there are any busy links to consumers.
 * @dev: Device to check.
 *
 * Check each consumer of the device and return 'true' if its link's status
 * is one of "consumer probe" or "active" (meaning that the given consumer is
 * probing right now or its driver is present).  Otherwise, change the link
 * state to "supplier unbind" to prevent the consumer from being probed
 * successfully going forward.
 *
 * Return 'false' if there are no probing or active consumers.
 *
 * Links without the DL_FLAG_MANAGED flag set are ignored.
 */
bool device_links_busy(struct device *dev)
{
	struct device_link *link;
	bool ret = false;

	device_links_write_lock();

	list_for_each_entry(link, &dev->links.consumers, s_node) {
		if (!(link->flags & DL_FLAG_MANAGED))
			continue;

		if (link->status == DL_STATE_CONSUMER_PROBE
		    || link->status == DL_STATE_ACTIVE) {
			ret = true;
			break;
		}
		WRITE_ONCE(link->status, DL_STATE_SUPPLIER_UNBIND);
	}

	dev->links.status = DL_DEV_UNBINDING;

	device_links_write_unlock();
	return ret;
}

/**
 * device_links_unbind_consumers - Force unbind consumers of the given device.
 * @dev: Device to unbind the consumers of.
 *
 * Walk the list of links to consumers for @dev and if any of them is in the
 * "consumer probe" state, wait for all device probes in progress to complete
 * and start over.
 *
 * If that's not the case, change the status of the link to "supplier unbind"
 * and check if the link was in the "active" state.  If so, force the consumer
 * driver to unbind and start over (the consumer will not re-probe as we have
 * changed the state of the link already).
 *
 * Links without the DL_FLAG_MANAGED flag set are ignored.
 */
void device_links_unbind_consumers(struct device *dev)
{
	struct device_link *link;

 start:
	device_links_write_lock();

	list_for_each_entry(link, &dev->links.consumers, s_node) {
		enum device_link_state status;

		if (!(link->flags & DL_FLAG_MANAGED) ||
		    link->flags & DL_FLAG_SYNC_STATE_ONLY)
			continue;

		status = link->status;
		if (status == DL_STATE_CONSUMER_PROBE) {
			device_links_write_unlock();

			wait_for_device_probe();
			goto start;
		}
		WRITE_ONCE(link->status, DL_STATE_SUPPLIER_UNBIND);
		if (status == DL_STATE_ACTIVE) {
			struct device *consumer = link->consumer;

			get_device(consumer);

			device_links_write_unlock();

			device_release_driver_internal(consumer, NULL,
						       consumer->parent);
			put_device(consumer);
			goto start;
		}
	}

	device_links_write_unlock();
}

/**
 * device_links_purge - Delete existing links to other devices.
 * @dev: Target device.
 */
static void device_links_purge(struct device *dev)
{
	struct device_link *link, *ln;

	if (dev->class == &devlink_class)
		return;

	/*
	 * Delete all of the remaining links from this device to any other
	 * devices (either consumers or suppliers).
	 */
	device_links_write_lock();

	list_for_each_entry_safe_reverse(link, ln, &dev->links.suppliers, c_node) {
		WARN_ON(link->status == DL_STATE_ACTIVE);
		__device_link_del(&link->kref);
	}

	list_for_each_entry_safe_reverse(link, ln, &dev->links.consumers, s_node) {
		WARN_ON(link->status != DL_STATE_DORMANT &&
			link->status != DL_STATE_NONE);
		__device_link_del(&link->kref);
	}

	device_links_write_unlock();
}

#define FW_DEVLINK_FLAGS_PERMISSIVE	(DL_FLAG_INFERRED | \
					 DL_FLAG_SYNC_STATE_ONLY)
#define FW_DEVLINK_FLAGS_ON		(DL_FLAG_INFERRED | \
					 DL_FLAG_AUTOPROBE_CONSUMER)
#define FW_DEVLINK_FLAGS_RPM		(FW_DEVLINK_FLAGS_ON | \
					 DL_FLAG_PM_RUNTIME)

static u32 fw_devlink_flags = FW_DEVLINK_FLAGS_ON;
static int __init fw_devlink_setup(char *arg)
{
	if (!arg)
		return -EINVAL;

	if (strcmp(arg, "off") == 0) {
		fw_devlink_flags = 0;
	} else if (strcmp(arg, "permissive") == 0) {
		fw_devlink_flags = FW_DEVLINK_FLAGS_PERMISSIVE;
	} else if (strcmp(arg, "on") == 0) {
		fw_devlink_flags = FW_DEVLINK_FLAGS_ON;
	} else if (strcmp(arg, "rpm") == 0) {
		fw_devlink_flags = FW_DEVLINK_FLAGS_RPM;
	}
	return 0;
}
early_param("fw_devlink", fw_devlink_setup);

static bool fw_devlink_strict;
static int __init fw_devlink_strict_setup(char *arg)
{
	return strtobool(arg, &fw_devlink_strict);
}
early_param("fw_devlink.strict", fw_devlink_strict_setup);

u32 fw_devlink_get_flags(void)
{
	return fw_devlink_flags;
}

static bool fw_devlink_is_permissive(void)
{
	return fw_devlink_flags == FW_DEVLINK_FLAGS_PERMISSIVE;
}

bool fw_devlink_is_strict(void)
{
	return fw_devlink_strict && !fw_devlink_is_permissive();
}

static void fw_devlink_parse_fwnode(struct fwnode_handle *fwnode)
{
	if (fwnode->flags & FWNODE_FLAG_LINKS_ADDED)
		return;

	fwnode_call_int_op(fwnode, add_links);
	fwnode->flags |= FWNODE_FLAG_LINKS_ADDED;
}

static void fw_devlink_parse_fwtree(struct fwnode_handle *fwnode)
{
	struct fwnode_handle *child = NULL;

	fw_devlink_parse_fwnode(fwnode);

	while ((child = fwnode_get_next_available_child_node(fwnode, child)))
		fw_devlink_parse_fwtree(child);
}

static void fw_devlink_relax_link(struct device_link *link)
{
	if (!(link->flags & DL_FLAG_INFERRED))
		return;

	if (link->flags == (DL_FLAG_MANAGED | FW_DEVLINK_FLAGS_PERMISSIVE))
		return;

	pm_runtime_drop_link(link);
	link->flags = DL_FLAG_MANAGED | FW_DEVLINK_FLAGS_PERMISSIVE;
	dev_dbg(link->consumer, "Relaxing link with %s\n",
		dev_name(link->supplier));
}

static int fw_devlink_no_driver(struct device *dev, void *data)
{
	struct device_link *link = to_devlink(dev);

	if (!link->supplier->can_match)
		fw_devlink_relax_link(link);

	return 0;
}

void fw_devlink_drivers_done(void)
{
	fw_devlink_drv_reg_done = true;
	device_links_write_lock();
	class_for_each_device(&devlink_class, NULL, NULL,
			      fw_devlink_no_driver);
	device_links_write_unlock();
}

static void fw_devlink_unblock_consumers(struct device *dev)
{
	struct device_link *link;

	if (!fw_devlink_flags || fw_devlink_is_permissive())
		return;

	device_links_write_lock();
	list_for_each_entry(link, &dev->links.consumers, s_node)
		fw_devlink_relax_link(link);
	device_links_write_unlock();
}

/**
 * fw_devlink_relax_cycle - Convert cyclic links to SYNC_STATE_ONLY links
 * @con: Device to check dependencies for.
 * @sup: Device to check against.
 *
 * Check if @sup depends on @con or any device dependent on it (its child or
 * its consumer etc).  When such a cyclic dependency is found, convert all
 * device links created solely by fw_devlink into SYNC_STATE_ONLY device links.
 * This is the equivalent of doing fw_devlink=permissive just between the
 * devices in the cycle. We need to do this because, at this point, fw_devlink
 * can't tell which of these dependencies is not a real dependency.
 *
 * Return 1 if a cycle is found. Otherwise, return 0.
 */
static int fw_devlink_relax_cycle(struct device *con, void *sup)
{
	struct device_link *link;
	int ret;

	if (con == sup)
		return 1;

	ret = device_for_each_child(con, sup, fw_devlink_relax_cycle);
	if (ret)
		return ret;

	list_for_each_entry(link, &con->links.consumers, s_node) {
		if ((link->flags & ~DL_FLAG_INFERRED) ==
		    (DL_FLAG_SYNC_STATE_ONLY | DL_FLAG_MANAGED))
			continue;

		if (!fw_devlink_relax_cycle(link->consumer, sup))
			continue;

		ret = 1;

		fw_devlink_relax_link(link);
	}
	return ret;
}

/**
 * fw_devlink_create_devlink - Create a device link from a consumer to fwnode
 * @con: consumer device for the device link
 * @sup_handle: fwnode handle of supplier
 * @flags: devlink flags
 *
 * This function will try to create a device link between the consumer device
 * @con and the supplier device represented by @sup_handle.
 *
 * The supplier has to be provided as a fwnode because incorrect cycles in
 * fwnode links can sometimes cause the supplier device to never be created.
 * This function detects such cases and returns an error if it cannot create a
 * device link from the consumer to a missing supplier.
 *
 * Returns,
 * 0 on successfully creating a device link
 * -EINVAL if the device link cannot be created as expected
 * -EAGAIN if the device link cannot be created right now, but it may be
 *  possible to do that in the future
 */
static int fw_devlink_create_devlink(struct device *con,
				     struct fwnode_handle *sup_handle, u32 flags)
{
	struct device *sup_dev;
	int ret = 0;

	/*
	 * In some cases, a device P might also be a supplier to its child node
	 * C. However, this would defer the probe of C until the probe of P
	 * completes successfully. This is perfectly fine in the device driver
	 * model. device_add() doesn't guarantee probe completion of the device
	 * by the time it returns.
	 *
	 * However, there are a few drivers that assume C will finish probing
	 * as soon as it's added and before P finishes probing. So, we provide
	 * a flag to let fw_devlink know not to delay the probe of C until the
	 * probe of P completes successfully.
	 *
	 * When such a flag is set, we can't create device links where P is the
	 * supplier of C as that would delay the probe of C.
	 */
	if (sup_handle->flags & FWNODE_FLAG_NEEDS_CHILD_BOUND_ON_ADD &&
	    fwnode_is_ancestor_of(sup_handle, con->fwnode))
		return -EINVAL;

	sup_dev = get_dev_from_fwnode(sup_handle);
	if (sup_dev) {
		/*
		 * If it's one of those drivers that don't actually bind to
		 * their device using driver core, then don't wait on this
		 * supplier device indefinitely.
		 */
		if (sup_dev->links.status == DL_DEV_NO_DRIVER &&
		    sup_handle->flags & FWNODE_FLAG_INITIALIZED) {
			ret = -EINVAL;
			goto out;
		}

		/*
		 * If this fails, it is due to cycles in device links.  Just
		 * give up on this link and treat it as invalid.
		 */
		if (!device_link_add(con, sup_dev, flags) &&
		    !(flags & DL_FLAG_SYNC_STATE_ONLY)) {
			dev_info(con, "Fixing up cyclic dependency with %s\n",
				 dev_name(sup_dev));
			device_links_write_lock();
			fw_devlink_relax_cycle(con, sup_dev);
			device_links_write_unlock();
			device_link_add(con, sup_dev,
					FW_DEVLINK_FLAGS_PERMISSIVE);
			ret = -EINVAL;
		}

		goto out;
	}

	/* Supplier that's already initialized without a struct device. */
	if (sup_handle->flags & FWNODE_FLAG_INITIALIZED)
		return -EINVAL;

	/*
	 * DL_FLAG_SYNC_STATE_ONLY doesn't block probing and supports
	 * cycles. So cycle detection isn't necessary and shouldn't be
	 * done.
	 */
	if (flags & DL_FLAG_SYNC_STATE_ONLY)
		return -EAGAIN;

	/*
	 * If we can't find the supplier device from its fwnode, it might be
	 * due to a cyclic dependency between fwnodes. Some of these cycles can
	 * be broken by applying logic. Check for these types of cycles and
	 * break them so that devices in the cycle probe properly.
	 *
	 * If the supplier's parent is dependent on the consumer, then the
	 * consumer and supplier have a cyclic dependency. Since fw_devlink
	 * can't tell which of the inferred dependencies are incorrect, don't
	 * enforce probe ordering between any of the devices in this cyclic
	 * dependency. Do this by relaxing all the fw_devlink device links in
	 * this cycle and by treating the fwnode link between the consumer and
	 * the supplier as an invalid dependency.
	 */
	sup_dev = fwnode_get_next_parent_dev(sup_handle);
	if (sup_dev && device_is_dependent(con, sup_dev)) {
		dev_info(con, "Fixing up cyclic dependency with %pfwP (%s)\n",
			 sup_handle, dev_name(sup_dev));
		device_links_write_lock();
		fw_devlink_relax_cycle(con, sup_dev);
		device_links_write_unlock();
		ret = -EINVAL;
	} else {
		/*
		 * Can't check for cycles or no cycles. So let's try
		 * again later.
		 */
		ret = -EAGAIN;
	}

out:
	put_device(sup_dev);
	return ret;
}

/**
 * __fw_devlink_link_to_consumers - Create device links to consumers of a device
 * @dev: Device that needs to be linked to its consumers
 *
 * This function looks at all the consumer fwnodes of @dev and creates device
 * links between the consumer device and @dev (supplier).
 *
 * If the consumer device has not been added yet, then this function creates a
 * SYNC_STATE_ONLY link between @dev (supplier) and the closest ancestor device
 * of the consumer fwnode. This is necessary to make sure @dev doesn't get a
 * sync_state() callback before the real consumer device gets to be added and
 * then probed.
 *
 * Once device links are created from the real consumer to @dev (supplier), the
 * fwnode links are deleted.
 */
static void __fw_devlink_link_to_consumers(struct device *dev)
{
	struct fwnode_handle *fwnode = dev->fwnode;
	struct fwnode_link *link, *tmp;

	list_for_each_entry_safe(link, tmp, &fwnode->consumers, s_hook) {
		u32 dl_flags = fw_devlink_get_flags();
		struct device *con_dev;
		bool own_link = true;
		int ret;

		con_dev = get_dev_from_fwnode(link->consumer);
		/*
		 * If consumer device is not available yet, make a "proxy"
		 * SYNC_STATE_ONLY link from the consumer's parent device to
		 * the supplier device. This is necessary to make sure the
		 * supplier doesn't get a sync_state() callback before the real
		 * consumer can create a device link to the supplier.
		 *
		 * This proxy link step is needed to handle the case where the
		 * consumer's parent device is added before the supplier.
		 */
		if (!con_dev) {
			con_dev = fwnode_get_next_parent_dev(link->consumer);
			/*
			 * However, if the consumer's parent device is also the
			 * parent of the supplier, don't create a
			 * consumer-supplier link from the parent to its child
			 * device. Such a dependency is impossible.
			 */
			if (con_dev &&
			    fwnode_is_ancestor_of(con_dev->fwnode, fwnode)) {
				put_device(con_dev);
				con_dev = NULL;
			} else {
				own_link = false;
				dl_flags = FW_DEVLINK_FLAGS_PERMISSIVE;
			}
		}

		if (!con_dev)
			continue;

		ret = fw_devlink_create_devlink(con_dev, fwnode, dl_flags);
		put_device(con_dev);
		if (!own_link || ret == -EAGAIN)
			continue;

		__fwnode_link_del(link);
	}
}

/**
 * __fw_devlink_link_to_suppliers - Create device links to suppliers of a device
 * @dev: The consumer device that needs to be linked to its suppliers
 * @fwnode: Root of the fwnode tree that is used to create device links
 *
 * This function looks at all the supplier fwnodes of fwnode tree rooted at
 * @fwnode and creates device links between @dev (consumer) and all the
 * supplier devices of the entire fwnode tree at @fwnode.
 *
 * The function creates normal (non-SYNC_STATE_ONLY) device links between @dev
 * and the real suppliers of @dev. Once these device links are created, the
 * fwnode links are deleted. When such device links are successfully created,
 * this function is called recursively on those supplier devices. This is
 * needed to detect and break some invalid cycles in fwnode links.  See
 * fw_devlink_create_devlink() for more details.
 *
 * In addition, it also looks at all the suppliers of the entire fwnode tree
 * because some of the child devices of @dev that have not been added yet
 * (because @dev hasn't probed) might already have their suppliers added to
 * driver core. So, this function creates SYNC_STATE_ONLY device links between
 * @dev (consumer) and these suppliers to make sure they don't execute their
 * sync_state() callbacks before these child devices have a chance to create
 * their device links. The fwnode links that correspond to the child devices
 * aren't delete because they are needed later to create the device links
 * between the real consumer and supplier devices.
 */
static void __fw_devlink_link_to_suppliers(struct device *dev,
					   struct fwnode_handle *fwnode)
{
	bool own_link = (dev->fwnode == fwnode);
	struct fwnode_link *link, *tmp;
	struct fwnode_handle *child = NULL;
	u32 dl_flags;

	if (own_link)
		dl_flags = fw_devlink_get_flags();
	else
		dl_flags = FW_DEVLINK_FLAGS_PERMISSIVE;

	list_for_each_entry_safe(link, tmp, &fwnode->suppliers, c_hook) {
		int ret;
		struct device *sup_dev;
		struct fwnode_handle *sup = link->supplier;

		ret = fw_devlink_create_devlink(dev, sup, dl_flags);
		if (!own_link || ret == -EAGAIN)
			continue;

		__fwnode_link_del(link);

		/* If no device link was created, nothing more to do. */
		if (ret)
			continue;

		/*
		 * If a device link was successfully created to a supplier, we
		 * now need to try and link the supplier to all its suppliers.
		 *
		 * This is needed to detect and delete false dependencies in
		 * fwnode links that haven't been converted to a device link
		 * yet. See comments in fw_devlink_create_devlink() for more
		 * details on the false dependency.
		 *
		 * Without deleting these false dependencies, some devices will
		 * never probe because they'll keep waiting for their false
		 * dependency fwnode links to be converted to device links.
		 */
		sup_dev = get_dev_from_fwnode(sup);
		__fw_devlink_link_to_suppliers(sup_dev, sup_dev->fwnode);
		put_device(sup_dev);
	}

	/*
	 * Make "proxy" SYNC_STATE_ONLY device links to represent the needs of
	 * all the descendants. This proxy link step is needed to handle the
	 * case where the supplier is added before the consumer's parent device
	 * (@dev).
	 */
	while ((child = fwnode_get_next_available_child_node(fwnode, child)))
		__fw_devlink_link_to_suppliers(dev, child);
}

static void fw_devlink_link_device(struct device *dev)
{
	struct fwnode_handle *fwnode = dev->fwnode;

	if (!fw_devlink_flags)
		return;

	fw_devlink_parse_fwtree(fwnode);

	mutex_lock(&fwnode_link_lock);
	__fw_devlink_link_to_consumers(dev);
	__fw_devlink_link_to_suppliers(dev, fwnode);
	mutex_unlock(&fwnode_link_lock);
}

int (*platform_notify)(struct device *dev) = NULL;
int (*platform_notify_remove)(struct device *dev) = NULL;

static DEFINE_MUTEX(device_hotplug_lock);

void lock_device_hotplug(void)
{
	mutex_lock(&device_hotplug_lock);
}

void unlock_device_hotplug(void)
{
	mutex_unlock(&device_hotplug_lock);
}

int lock_device_hotplug_sysfs(void)
{
	return 0;
}

#ifdef CONFIG_BLOCK
static inline int device_is_not_partition(struct device *dev)
{
	return !(dev->type == &part_type);
}
#else
static inline int device_is_not_partition(struct device *dev)
{
	return 1;
}
#endif

static void device_platform_notify(struct device *dev)
{
	// acpi_device_notify(dev);

	software_node_notify(dev);

	if (platform_notify)
		platform_notify(dev);
}

static void device_platform_notify_remove(struct device *dev)
{
	// acpi_device_notify_remove(dev);

	software_node_notify_remove(dev);

	if (platform_notify_remove)
		platform_notify_remove(dev);
}

/**
 * dev_driver_string - Return a device's driver name, if at all possible
 * @dev: struct device to get the name of
 *
 * Will return the device's driver's name if it is bound to a device.  If
 * the device is not bound to a driver, it will return the name of the bus
 * it is attached to.  If it is not attached to a bus either, an empty
 * string will be returned.
 */
const char *dev_driver_string(const struct device *dev)
{
	struct device_driver *drv;

	/* dev->driver can change to NULL underneath us because of unbinding,
	 * so be careful about accessing it.  dev->bus and dev->class should
	 * never change once they are set, so they don't need special care.
	 */
	drv = READ_ONCE(dev->driver);
	return drv ? drv->name : dev_bus_name(dev);
}
EXPORT_SYMBOL(dev_driver_string);

static void klist_children_get(struct klist_node *n)
{
	struct device_private *p = to_device_private_parent(n);
	struct device *dev = p->device;

	// get_device(dev);
}

static void klist_children_put(struct klist_node *n)
{
	struct device_private *p = to_device_private_parent(n);
	struct device *dev = p->device;

	// put_device(dev);
}

/* register */

/**
 * device_initialize - init device structure.
 * @dev: device.
 *
 * This prepares the device for use by other layers by initializing
 * its fields.
 * It is the first half of device_register(), if called by
 * that function, though it can also be called separately, so one
 * may use @dev's fields. In particular, get_device()/put_device()
 * may be used for reference counting of @dev after calling this
 * function.
 *
 * All fields in @dev must be initialized by the caller to 0, except
 * for those explicitly set to some other value.  The simplest
 * approach is to use kzalloc() to allocate the structure containing
 * @dev.
 *
 * NOTE: Use put_device() to give up your reference instead of freeing
 * @dev directly once you have called this function.
 */
void device_initialize(struct device *dev)
{
	INIT_LIST_HEAD(&dev->dma_pools);
	mutex_init(&dev->mutex);
#ifdef CONFIG_PROVE_LOCKING
	mutex_init(&dev->lockdep_mutex);
#endif
	// lockdep_set_novalidate_class(&dev->mutex);
	spin_lock_init(&dev->devres_lock);
	INIT_LIST_HEAD(&dev->devres_head);
	device_pm_init(dev);
	set_dev_node(dev, -1);
	INIT_LIST_HEAD(&dev->links.consumers);
	INIT_LIST_HEAD(&dev->links.suppliers);
	INIT_LIST_HEAD(&dev->links.defer_sync);
	dev->links.status = DL_DEV_NO_DRIVER;
#if defined(CONFIG_ARCH_HAS_SYNC_DMA_FOR_DEVICE) || \
    defined(CONFIG_ARCH_HAS_SYNC_DMA_FOR_CPU) || \
    defined(CONFIG_ARCH_HAS_SYNC_DMA_FOR_CPU_ALL)
	dev->dma_coherent = 0;
#endif
#ifdef CONFIG_SWIOTLB
	dev->dma_io_tlb_mem = 0;
#endif
}
EXPORT_SYMBOL_GPL(device_initialize);

/**
 * dev_set_name - set a device name
 * @dev: device
 * @fmt: format string for the device's name
 */
int dev_set_name(struct device *dev, const char *fmt, ...)
{
	va_list vargs;
	int err;

	va_start(vargs, fmt);
	err = 0; // kobject_set_name_vargs(&dev->kobj, fmt, vargs);
	va_end(vargs);
	return err;
}
EXPORT_SYMBOL_GPL(dev_set_name);

static int device_private_init(struct device *dev)
{
	dev->p = kzalloc(sizeof(*dev->p), 0);
	if (!dev->p)
		return -ENOMEM;
	dev->p->device = dev;
	klist_init(&dev->p->klist_children, klist_children_get,
		   klist_children_put);
	INIT_LIST_HEAD(&dev->p->deferred_probe);
	return 0;
}

/**
 * device_add - add device to device hierarchy.
 * @dev: device.
 *
 * This is part 2 of device_register(), though may be called
 * separately _iff_ device_initialize() has been called separately.
 *
 * This adds @dev to the kobject hierarchy via kobject_add(), adds it
 * to the global and sibling lists for the device, then
 * adds it to the other relevant subsystems of the driver model.
 *
 * Do not call this routine or device_register() more than once for
 * any device structure.  The driver model core is not designed to work
 * with devices that get unregistered and then spring back to life.
 * (Among other things, it's very hard to guarantee that all references
 * to the previous incarnation of @dev have been dropped.)  Allocate
 * and register a fresh new struct device instead.
 *
 * NOTE: _Never_ directly free @dev after calling this function, even
 * if it returned an error! Always use put_device() to give up your
 * reference instead.
 *
 * Rule of thumb is: if device_add() succeeds, you should call
 * device_del() when you want to get rid of it. If device_add() has
 * *not* succeeded, use *only* put_device() to drop the reference
 * count.
 */
int device_add(struct device *dev)
{
	struct device *parent;
	struct class_interface *class_intf;
	int error = -EINVAL;

	dev = get_device(dev);
	if (!dev)
		goto done;

	if (!dev->p) {
		error = device_private_init(dev);
		if (error)
			goto done;
	}

	/*
	 * for statically allocated devices, which should all be converted
	 * some day, we need to initialize the name. We prevent reading back
	 * the name, and force the use of dev_name()
	 */
	if (dev->init_name) {
		dev_set_name(dev, "%s", dev->init_name);
		dev->init_name = NULL;
	}

	/* subsystems can specify simple device enumeration */
	if (!dev_name(dev) && dev->bus && dev->bus->dev_name)
		dev_set_name(dev, "%s%u", dev->bus->dev_name, dev->id);

	if (!dev_name(dev)) {
		error = -EINVAL;
		goto name_error;
	}

	pr_debug("device: '%s': %s\n", dev_name(dev), __func__);

	parent = get_device(dev->parent);

	/* use parent numa_node */
	if (parent && (dev_to_node(dev) == -1))
		set_dev_node(dev, dev_to_node(parent));

	/* first, register with generic layer. */
	/* notify platform of device entry */
	device_platform_notify(dev);

	error = bus_add_device(dev);
	if (error)
		goto BusError;

	device_pm_add(dev);

	/* Notify clients of device addition.  This call must come
	 * after dpm_sysfs_add() and before kobject_uevent().
	 */
	if (dev->bus)
		blocking_notifier_call_chain(&dev->bus->p->bus_notifier,
					     BUS_NOTIFY_ADD_DEVICE, dev);

	/*
	 * Check if any of the other devices (consumers) have been waiting for
	 * this device (supplier) to be added so that they can create a device
	 * link to it.
	 *
	 * This needs to happen after device_pm_add() because device_link_add()
	 * requires the supplier be registered before it's called.
	 *
	 * But this also needs to happen before bus_probe_device() to make sure
	 * waiting consumers can link to it before the driver is bound to the
	 * device and the driver sync_state callback is called for this device.
	 */
	if (dev->fwnode && !dev->fwnode->dev) {
		dev->fwnode->dev = dev;
		fw_devlink_link_device(dev);
	}

	bus_probe_device(dev);

	/*
	 * If all driver registration is done and a newly added device doesn't
	 * match with any driver, don't block its consumers from probing in
	 * case the consumer device is able to operate without this supplier.
	 */
	if (dev->fwnode && fw_devlink_drv_reg_done && !dev->can_match)
		fw_devlink_unblock_consumers(dev);

	if (parent)
		klist_add_tail(&dev->p->knode_parent,
			       &parent->p->klist_children);

	if (dev->class) {
		mutex_lock(&dev->class->p->mutex);
		/* tie the class to the device */
		klist_add_tail(&dev->p->knode_class,
			       &dev->class->p->klist_devices);

		/* notify any interfaces that the device is here */
		list_for_each_entry(class_intf,
				    &dev->class->p->interfaces, node)
			if (class_intf->add_dev)
				class_intf->add_dev(dev, class_intf);
		mutex_unlock(&dev->class->p->mutex);
	}
done:
	put_device(dev);
	return error;
BusError:
	device_platform_notify_remove(dev);
name_error:
	kfree(dev->p);
	dev->p = NULL;
	goto done;
}
EXPORT_SYMBOL_GPL(device_add);

/**
 * device_register - register a device with the system.
 * @dev: pointer to the device structure
 *
 * This happens in two clean steps - initialize the device
 * and add it to the system. The two steps can be called
 * separately, but this is the easiest and most common.
 * I.e. you should only call the two helpers separately if
 * have a clearly defined need to use and refcount the device
 * before it is added to the hierarchy.
 *
 * For more information, see the kerneldoc for device_initialize()
 * and device_add().
 *
 * NOTE: _Never_ directly free @dev after calling this function, even
 * if it returned an error! Always use put_device() to give up the
 * reference initialized in this function instead.
 */
int device_register(struct device *dev)
{
	device_initialize(dev);
	return device_add(dev);
}
EXPORT_SYMBOL_GPL(device_register);

/**
 * get_device - increment reference count for device.
 * @dev: device.
 *
 * This simply forwards the call to kobject_get(), though
 * we do take care to provide for the case that we get a NULL
 * pointer passed in.
 */
struct device *get_device(struct device *dev)
{
	return dev;
}
EXPORT_SYMBOL_GPL(get_device);

/**
 * put_device - decrement reference count.
 * @dev: device in question.
 */
void put_device(struct device *dev)
{

}
EXPORT_SYMBOL_GPL(put_device);

bool kill_device(struct device *dev)
{
	/*
	 * Require the device lock and set the "dead" flag to guarantee that
	 * the update behavior is consistent with the other bitfields near
	 * it and that we cannot have an asynchronous probe routine trying
	 * to run while we are tearing out the bus/class/sysfs from
	 * underneath the device.
	 */
	device_lock_assert(dev);

	if (dev->p->dead)
		return false;
	dev->p->dead = true;
	return true;
}
EXPORT_SYMBOL_GPL(kill_device);

/**
 * device_del - delete device from system.
 * @dev: device.
 *
 * This is the first part of the device unregistration
 * sequence. This removes the device from the lists we control
 * from here, has it removed from the other driver model
 * subsystems it was added to in device_add(), and removes it
 * from the kobject hierarchy.
 *
 * NOTE: this should be called manually _iff_ device_add() was
 * also called manually.
 */
void device_del(struct device *dev)
{
	struct device *parent = dev->parent;
	struct class_interface *class_intf;

	device_lock(dev);
	kill_device(dev);
	device_unlock(dev);

	if (dev->fwnode && dev->fwnode->dev == dev)
		dev->fwnode->dev = NULL;

	if (dev->bus)
		blocking_notifier_call_chain(&dev->bus->p->bus_notifier,
					     BUS_NOTIFY_DEL_DEVICE, dev);
	if (parent)
		klist_del(&dev->p->knode_parent);

	if (dev->class) {
		mutex_lock(&dev->class->p->mutex);
		/* notify any interfaces that the device is now gone */
		list_for_each_entry(class_intf,
				    &dev->class->p->interfaces, node)
			if (class_intf->remove_dev)
				class_intf->remove_dev(dev, class_intf);
		/* remove the device from the class list */
		klist_del(&dev->p->knode_class);
		mutex_unlock(&dev->class->p->mutex);
	}

	bus_remove_device(dev);
	device_pm_remove(dev);
	//driver_deferred_probe_del(dev);
	device_platform_notify_remove(dev);
	device_links_purge(dev);

	if (dev->bus)
		blocking_notifier_call_chain(&dev->bus->p->bus_notifier,
					     BUS_NOTIFY_REMOVED_DEVICE, dev);
	put_device(parent);
}
EXPORT_SYMBOL_GPL(device_del);

/**
 * device_unregister - unregister device from system.
 * @dev: device going away.
 *
 * We do this in two parts, like we do device_register(). First,
 * we remove it from all the subsystems with device_del(), then
 * we decrement the reference count via put_device(). If that
 * is the final reference count, the device will be cleaned up
 * via device_release() above. Otherwise, the structure will
 * stick around until the final reference to the device is dropped.
 */
void device_unregister(struct device *dev)
{
	pr_debug("device: '%s': %s\n", dev_name(dev), __func__);
	device_del(dev);
	put_device(dev);
}
EXPORT_SYMBOL_GPL(device_unregister);

static struct device *prev_device(struct klist_iter *i)
{
	struct klist_node *n = klist_prev(i);
	struct device *dev = NULL;
	struct device_private *p;

	if (n) {
		p = to_device_private_parent(n);
		dev = p->device;
	}
	return dev;
}

static struct device *next_device(struct klist_iter *i)
{
	struct klist_node *n = klist_next(i);
	struct device *dev = NULL;
	struct device_private *p;

	if (n) {
		p = to_device_private_parent(n);
		dev = p->device;
	}
	return dev;
}

/**
 * device_for_each_child - device child iterator.
 * @parent: parent struct device.
 * @fn: function to be called for each device.
 * @data: data for the callback.
 *
 * Iterate over @parent's child devices, and call @fn for each,
 * passing it @data.
 *
 * We check the return of @fn each time. If it returns anything
 * other than 0, we break out and return that value.
 */
int device_for_each_child(struct device *parent, void *data,
			  int (*fn)(struct device *dev, void *data))
{
	struct klist_iter i;
	struct device *child;
	int error = 0;

	if (!parent->p)
		return 0;

	klist_iter_init(&parent->p->klist_children, &i);
	while (!error && (child = next_device(&i)))
		error = fn(child, data);
	klist_iter_exit(&i);
	return error;
}
EXPORT_SYMBOL_GPL(device_for_each_child);

/**
 * device_for_each_child_reverse - device child iterator in reversed order.
 * @parent: parent struct device.
 * @fn: function to be called for each device.
 * @data: data for the callback.
 *
 * Iterate over @parent's child devices, and call @fn for each,
 * passing it @data.
 *
 * We check the return of @fn each time. If it returns anything
 * other than 0, we break out and return that value.
 */
int device_for_each_child_reverse(struct device *parent, void *data,
				  int (*fn)(struct device *dev, void *data))
{
	struct klist_iter i;
	struct device *child;
	int error = 0;

	if (!parent->p)
		return 0;

	klist_iter_init(&parent->p->klist_children, &i);
	while ((child = prev_device(&i)) && !error)
		error = fn(child, data);
	klist_iter_exit(&i);
	return error;
}
EXPORT_SYMBOL_GPL(device_for_each_child_reverse);

/**
 * device_find_child - device iterator for locating a particular device.
 * @parent: parent struct device
 * @match: Callback function to check device
 * @data: Data to pass to match function
 *
 * This is similar to the device_for_each_child() function above, but it
 * returns a reference to a device that is 'found' for later use, as
 * determined by the @match callback.
 *
 * The callback should return 0 if the device doesn't match and non-zero
 * if it does.  If the callback returns non-zero and a reference to the
 * current device can be obtained, this function will return to the caller
 * and not iterate over any more devices.
 *
 * NOTE: you will need to drop the reference with put_device() after use.
 */
struct device *device_find_child(struct device *parent, void *data,
				 int (*match)(struct device *dev, void *data))
{
	struct klist_iter i;
	struct device *child;

	if (!parent)
		return NULL;

	klist_iter_init(&parent->p->klist_children, &i);
	while ((child = next_device(&i)))
		if (match(child, data) && get_device(child))
			break;
	klist_iter_exit(&i);
	return child;
}
EXPORT_SYMBOL_GPL(device_find_child);

/**
 * device_find_child_by_name - device iterator for locating a child device.
 * @parent: parent struct device
 * @name: name of the child device
 *
 * This is similar to the device_find_child() function above, but it
 * returns a reference to a device that has the name @name.
 *
 * NOTE: you will need to drop the reference with put_device() after use.
 */
struct device *device_find_child_by_name(struct device *parent,
					 const char *name)
{
	struct klist_iter i;
	struct device *child;

	if (!parent)
		return NULL;

	klist_iter_init(&parent->p->klist_children, &i);
	while ((child = next_device(&i)))
		if (sysfs_streq(dev_name(child), name) && get_device(child))
			break;
	klist_iter_exit(&i);
	return child;
}
EXPORT_SYMBOL_GPL(device_find_child_by_name);

static int device_check_offline(struct device *dev, void *not_used)
{
	int ret;

	ret = device_for_each_child(dev, NULL, device_check_offline);
	if (ret)
		return ret;

	return device_supports_offline(dev) && !dev->offline ? -EBUSY : 0;
}

/**
 * device_offline - Prepare the device for hot-removal.
 * @dev: Device to be put offline.
 *
 * Execute the device bus type's .offline() callback, if present, to prepare
 * the device for a subsequent hot-removal.  If that succeeds, the device must
 * not be used until either it is removed or its bus type's .online() callback
 * is executed.
 *
 * Call under device_hotplug_lock.
 */
int device_offline(struct device *dev)
{
	int ret;

	if (dev->offline_disabled)
		return -EPERM;

	ret = device_for_each_child(dev, NULL, device_check_offline);
	if (ret)
		return ret;

	device_lock(dev);
	if (device_supports_offline(dev)) {
		if (dev->offline) {
			ret = 1;
		} else {
			ret = dev->bus->offline(dev);
			if (!ret) {
				dev->offline = true;
			}
		}
	}
	device_unlock(dev);

	return ret;
}

/**
 * device_online - Put the device back online after successful device_offline().
 * @dev: Device to be put back online.
 *
 * If device_offline() has been successfully executed for @dev, but the device
 * has not been removed subsequently, execute its bus type's .online() callback
 * to indicate that the device can be used again.
 *
 * Call under device_hotplug_lock.
 */
int device_online(struct device *dev)
{
	int ret = 0;

	device_lock(dev);
	if (device_supports_offline(dev)) {
		if (dev->offline) {
			ret = dev->bus->online(dev);
			if (!ret) {
				dev->offline = false;
			}
		} else {
			ret = 1;
		}
	}
	device_unlock(dev);

	return ret;
}

struct root_device {
	struct device dev;
};

static inline struct root_device *to_root_device(struct device *d)
{
	return container_of(d, struct root_device, dev);
}

static void root_device_release(struct device *dev)
{
	kfree(to_root_device(dev));
}

/**
 * __root_device_register - allocate and register a root device
 * @name: root device name
 * @owner: owner module of the root device, usually THIS_MODULE
 *
 * This function allocates a root device and registers it
 * using device_register(). In order to free the returned
 * device, use root_device_unregister().
 *
 * Root devices are dummy devices which allow other devices
 * to be grouped under /sys/devices. Use this function to
 * allocate a root device and then use it as the parent of
 * any device which should appear under /sys/devices/{name}
 *
 * The /sys/devices/{name} directory will also contain a
 * 'module' symlink which points to the @owner directory
 * in sysfs.
 *
 * Returns &struct device pointer on success, or ERR_PTR() on error.
 *
 * Note: You probably want to use root_device_register().
 */
struct device *__root_device_register(const char *name)
{
	struct root_device *root;
	int err = -ENOMEM;

	root = kzalloc(sizeof(struct root_device), 0);
	if (!root)
		return ERR_PTR(err);

	err = dev_set_name(&root->dev, "%s", name);
	if (err) {
		kfree(root);
		return ERR_PTR(err);
	}

	root->dev.release = root_device_release;

	err = device_register(&root->dev);
	if (err) {
		put_device(&root->dev);
		return ERR_PTR(err);
	}

	return &root->dev;
}
EXPORT_SYMBOL_GPL(__root_device_register);

/**
 * root_device_unregister - unregister and free a root device
 * @dev: device going away
 *
 * This function unregisters and cleans up a device that was created by
 * root_device_register().
 */
void root_device_unregister(struct device *dev)
{
	struct root_device *root = to_root_device(dev);

	device_unregister(dev);
}
EXPORT_SYMBOL_GPL(root_device_unregister);


static void device_create_release(struct device *dev)
{
	pr_debug("device: '%s': %s\n", dev_name(dev), __func__);
	kfree(dev);
}

static struct device *
device_create_groups_vargs(struct class *class, struct device *parent,
			   dev_t devt, void *drvdata)
{
	struct device *dev = NULL;
	int retval = -ENODEV;

	if (class == NULL || IS_ERR(class))
		goto error;

	dev = kzalloc(sizeof(*dev), 0);
	if (!dev) {
		retval = -ENOMEM;
		goto error;
	}

	device_initialize(dev);
	dev->devt = devt;
	dev->class = class;
	dev->parent = parent;
	dev->release = device_create_release;
	dev_set_drvdata(dev, drvdata);

	retval = device_add(dev);
	if (retval)
		goto error;

	return dev;

error:
	put_device(dev);
	return ERR_PTR(retval);
}

/**
 * device_create - creates a device and registers it with sysfs
 * @class: pointer to the struct class that this device should be registered to
 * @parent: pointer to the parent struct device of this new device, if any
 * @devt: the dev_t for the char device to be added
 * @drvdata: the data to be added to the device for callbacks
 * @fmt: string for the device's name
 *
 * This function can be used by char device classes.  A struct device
 * will be created in sysfs, registered to the specified class.
 *
 * A "dev" file will be created, showing the dev_t for the device, if
 * the dev_t is not 0,0.
 * If a pointer to a parent struct device is passed in, the newly created
 * struct device will be a child of that device in sysfs.
 * The pointer to the struct device will be returned from the call.
 * Any further sysfs files that might be required can be created using this
 * pointer.
 *
 * Returns &struct device pointer on success, or ERR_PTR() on error.
 *
 * Note: the struct class passed to this function must have previously
 * been created with a call to class_create().
 */
struct device *device_create(struct class *class, struct device *parent,
			     dev_t devt, void *drvdata)
{
	struct device *dev;

	dev = device_create_groups_vargs(class, parent, devt, drvdata);
	return dev;
}
EXPORT_SYMBOL_GPL(device_create);

/**
 * device_destroy - removes a device that was created with device_create()
 * @class: pointer to the struct class that this device was registered with
 * @devt: the dev_t of the device that was previously registered
 *
 * This call unregisters and cleans up a device that was created with a
 * call to device_create().
 */
void device_destroy(struct class *class, dev_t devt)
{
	struct device *dev;

	dev = class_find_device_by_devt(class, devt);
	if (dev) {
		put_device(dev);
		device_unregister(dev);
	}
}
EXPORT_SYMBOL_GPL(device_destroy);

/**
 * device_move - moves a device to a new parent
 * @dev: the pointer to the struct device to be moved
 * @new_parent: the new parent of the device (can be NULL)
 * @dpm_order: how to reorder the dpm_list
 */
int device_move(struct device *dev, struct device *new_parent,
		enum dpm_order dpm_order)
{
	int error;
	struct device *old_parent;

	dev = get_device(dev);
	if (!dev)
		return -EINVAL;

	device_pm_lock();
	new_parent = get_device(new_parent);

	pr_debug("device: '%s': %s: moving to '%s'\n", dev_name(dev),
		 __func__, new_parent ? dev_name(new_parent) : "<NULL>");

	old_parent = dev->parent;
	dev->parent = new_parent;
	if (old_parent)
		klist_remove(&dev->p->knode_parent);
	if (new_parent) {
		klist_add_tail(&dev->p->knode_parent,
			       &new_parent->p->klist_children);
		set_dev_node(dev, dev_to_node(new_parent));
	}

	switch (dpm_order) {
	case DPM_ORDER_NONE:
		break;
	case DPM_ORDER_DEV_AFTER_PARENT:
		device_pm_move_after(dev, new_parent);
		break;
	case DPM_ORDER_PARENT_BEFORE_DEV:
		device_pm_move_before(new_parent, dev);
		break;
	case DPM_ORDER_DEV_LAST:
		device_pm_move_last(dev);
		break;
	}

	put_device(old_parent);
	return 0;
}
EXPORT_SYMBOL_GPL(device_move);


/**
 * dev_err_probe - probe error check and log helper
 * @dev: the pointer to the struct device
 * @err: error value to test
 * @fmt: printf-style format string
 * @...: arguments as specified in the format string
 *
 * This helper implements common pattern present in probe functions for error
 * checking: print debug or error message depending if the error value is
 * -EPROBE_DEFER and propagate error upwards.
 * In case of -EPROBE_DEFER it sets also defer probe reason, which can be
 * checked later by reading devices_deferred debugfs attribute.
 * It replaces code sequence::
 *
 * 	if (err != -EPROBE_DEFER)
 * 		dev_err(dev, ...);
 * 	else
 * 		dev_dbg(dev, ...);
 * 	return err;
 *
 * with::
 *
 * 	return dev_err_probe(dev, err, ...);
 *
 * Note that it is deemed acceptable to use this function for error
 * prints during probe even if the @err is known to never be -EPROBE_DEFER.
 * The benefit compared to a normal dev_err() is the standardized format
 * of the error code and the fact that the error code is returned.
 *
 * Returns @err.
 *
 */
int dev_err_probe(const struct device *dev, int err, const char *fmt, ...)
{
	struct va_format vaf;
	va_list args;

	va_start(args, fmt);
	vaf.fmt = fmt;
	vaf.va = &args;

	if (err != -EPROBE_DEFER) {
		dev_err(dev, "error %pe: %pV", ERR_PTR(err), &vaf);
	} else {
		// device_set_deferred_probe_reason(dev, &vaf);
		dev_dbg(dev, "error %pe: %pV", ERR_PTR(err), &vaf);
	}

	va_end(args);

	return err;
}
EXPORT_SYMBOL_GPL(dev_err_probe);

static inline bool fwnode_is_primary(struct fwnode_handle *fwnode)
{
	return fwnode && !IS_ERR(fwnode->secondary);
}

/**
 * set_primary_fwnode - Change the primary firmware node of a given device.
 * @dev: Device to handle.
 * @fwnode: New primary firmware node of the device.
 *
 * Set the device's firmware node pointer to @fwnode, but if a secondary
 * firmware node of the device is present, preserve it.
 *
 * Valid fwnode cases are:
 *  - primary --> secondary --> -ENODEV
 *  - primary --> NULL
 *  - secondary --> -ENODEV
 *  - NULL
 */
void set_primary_fwnode(struct device *dev, struct fwnode_handle *fwnode)
{
	struct device *parent = dev->parent;
	struct fwnode_handle *fn = dev->fwnode;

	if (fwnode) {
		if (fwnode_is_primary(fn))
			fn = fn->secondary;

		if (fn) {
			WARN_ON(fwnode->secondary);
			fwnode->secondary = fn;
		}
		dev->fwnode = fwnode;
	} else {
		if (fwnode_is_primary(fn)) {
			dev->fwnode = fn->secondary;
			/* Set fn->secondary = NULL, so fn remains the primary fwnode */
			if (!(parent && fn == parent->fwnode))
				fn->secondary = NULL;
		} else {
			dev->fwnode = NULL;
		}
	}
}
EXPORT_SYMBOL_GPL(set_primary_fwnode);

/**
 * set_secondary_fwnode - Change the secondary firmware node of a given device.
 * @dev: Device to handle.
 * @fwnode: New secondary firmware node of the device.
 *
 * If a primary firmware node of the device is present, set its secondary
 * pointer to @fwnode.  Otherwise, set the device's firmware node pointer to
 * @fwnode.
 */
void set_secondary_fwnode(struct device *dev, struct fwnode_handle *fwnode)
{
	if (fwnode)
		fwnode->secondary = ERR_PTR(-ENODEV);

	if (fwnode_is_primary(dev->fwnode))
		dev->fwnode->secondary = fwnode;
	else
		dev->fwnode = fwnode;
}
EXPORT_SYMBOL_GPL(set_secondary_fwnode);

/**
 * device_set_of_node_from_dev - reuse device-tree node of another device
 * @dev: device whose device-tree node is being set
 * @dev2: device whose device-tree node is being reused
 *
 * Takes another reference to the new device-tree node after first dropping
 * any reference held to the old node.
 */
void device_set_of_node_from_dev(struct device *dev, const struct device *dev2)
{
	of_node_put(dev->of_node);
	dev->of_node = of_node_get(dev2->of_node);
	dev->of_node_reused = true;
}
EXPORT_SYMBOL_GPL(device_set_of_node_from_dev);

void device_set_node(struct device *dev, struct fwnode_handle *fwnode)
{
	dev->fwnode = fwnode;
	dev->of_node = to_of_node(fwnode);
}
EXPORT_SYMBOL_GPL(device_set_node);

int device_match_name(struct device *dev, const void *name)
{
	return sysfs_streq(dev_name(dev), name);
}
EXPORT_SYMBOL_GPL(device_match_name);

int device_match_of_node(struct device *dev, const void *np)
{
	return dev->of_node == np;
}
EXPORT_SYMBOL_GPL(device_match_of_node);

int device_match_fwnode(struct device *dev, const void *fwnode)
{
	return dev_fwnode(dev) == fwnode;
}
EXPORT_SYMBOL_GPL(device_match_fwnode);

int device_match_devt(struct device *dev, const void *pdevt)
{
	return dev->devt == *(dev_t *)pdevt;
}
EXPORT_SYMBOL_GPL(device_match_devt);

int device_match_acpi_dev(struct device *dev, const void *adev)
{
	return 0; // ACPI_COMPANION(dev) == adev;
}
EXPORT_SYMBOL(device_match_acpi_dev);

int device_match_acpi_handle(struct device *dev, const void *handle)
{
	return 0; // ACPI_HANDLE(dev) == handle;
}
EXPORT_SYMBOL(device_match_acpi_handle);

int device_match_any(struct device *dev, const void *unused)
{
	return 1;
}
EXPORT_SYMBOL_GPL(device_match_any);
