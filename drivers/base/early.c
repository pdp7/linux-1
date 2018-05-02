// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2018 Texas Instruments, Inc.
 *
 * Author:
 *     Bartosz Golaszewski <bgolaszewski@baylibre.com>
 */

#include <linux/early_platform.h>
#include <linux/init.h>
#include <linux/of.h>
#include <linux/of_platform.h>
#include <linux/slab.h>

#include "base.h"

extern struct early_platform_driver *__early_platform_drivers_table[];
extern struct early_platform_driver *__early_platform_drivers_table_end[];

static bool early_platform_done;

static LIST_HEAD(early_platform_drivers);
static LIST_HEAD(early_platform_devices);

static int early_platform_device_set_name(struct early_platform_device *edev)
{
	switch (edev->pdev.id) {
	case PLATFORM_DEVID_AUTO:
		pr_warn("auto device ID not supported in early platform devices\n");
		/* fallthrough */
	case PLATFORM_DEVID_NONE:
		edev->pdev.dev.init_name = kasprintf(GFP_KERNEL,
						     "%s", edev->pdev.name);
		break;
	default:
		edev->pdev.dev.init_name = kasprintf(GFP_KERNEL, "%s.%d",
						     edev->pdev.name,
						     edev->pdev.id);
		break;
	}

	if (!edev->pdev.dev.init_name)
		return -ENOMEM;

	return 0;
}

static void early_platform_device_add(struct early_platform_device *edev)
{
	edev->pdev.dev.early = true;
	INIT_LIST_HEAD(&edev->list);
	list_add_tail(&edev->list, &early_platform_devices);
}

static void early_platform_probe_deferred(void)
{
	struct early_platform_device *edev;
	int rv;

	list_for_each_entry(edev, &early_platform_devices, list) {
		if (!edev->deferred || !edev->deferred_drv->early_probe)
			continue;

		rv = edev->deferred_drv->early_probe(&edev->pdev);
		if (rv && rv != -EPROBE_DEFER) {
			dev_err(&edev->pdev.dev,
				"early platform driver probe failed: %d\n",
				rv);
		}
	}
}

static void early_platform_try_probe(struct early_platform_driver *edrv,
				     struct early_platform_device *edev)
{
	int rv;

	rv = early_platform_device_set_name(edev);
	if (rv)
		pr_warn("unable to set the early platform device name\n");

	if (edrv->early_probe) {
		rv = edrv->early_probe(&edev->pdev);
		if (rv && rv != -EPROBE_DEFER &&
		    rv != -ENODEV && rv != -ENXIO) {
			dev_err(&edev->pdev.dev,
				"early platform driver probe failed: %d\n",
				rv);
			return;
		} else if (rv == -EPROBE_DEFER) {
			edev->deferred = true;
			edev->deferred_drv = edrv;
		} else {
			early_platform_probe_deferred();
		}
	}
}

/**
 * of_early_to_platform_device - return the platform device with which this
 *                               device node is associated
 * @np - device node to look up
 *
 * If a device node was populated early, the corresponding platform device
 * already exists. Instead of allocating a new object, we need to retrieve
 * the previous one. This routine enables it.
 */
struct platform_device *of_early_to_platform_device(struct device_node *np)
{
	struct early_platform_device *edev;

	list_for_each_entry(edev, &early_platform_devices, list) {
		if (np == edev->pdev.dev.of_node)
			return &edev->pdev;
	}

	return ERR_PTR(-ENOENT);
}

static int of_early_platform_device_create(struct device_node *node,
					   struct early_platform_driver *edrv)
{
	struct early_platform_device *edev;
	int rc;

	edev = kzalloc(sizeof(*edev), GFP_KERNEL);
	if (!edev)
		return -ENOMEM;

	platform_device_init(&edev->pdev, "", PLATFORM_DEVID_NONE);
	/*
	 * We can safely use platform_device_release since the platform_device
	 * struct is the first member of early_platform_device.
	 */
	edev->pdev.dev.release = platform_device_release;

	rc = of_device_init_resources(&edev->pdev, node);
	if (rc) {
		kfree(edev);
		return rc;
	}

	of_node_set_flag(node, OF_POPULATED_EARLY);
	edev->pdev.name = edrv->pdrv.driver.name;
	edev->pdev.dev.of_node = of_node_get(node);
	edev->pdev.dev.fwnode = &node->fwnode;
	early_platform_device_add(edev);
	early_platform_try_probe(edrv, edev);

	return 0;
}

static int of_early_platform_populate(struct device_node *root)
{
	struct early_platform_driver *edrv;
	const struct of_device_id *match;
	struct device_node *child;
	int rv;

	if (!root)
		return 0;

	list_for_each_entry(edrv, &early_platform_drivers, list) {
		if (!edrv->pdrv.driver.of_match_table)
			continue;

		match = of_match_node(edrv->pdrv.driver.of_match_table, root);
		if (!match)
			continue;

		rv = of_early_platform_device_create(root, edrv);
		if (rv)
			return rv;
	}

	for_each_child_of_node(root, child) {
		rv = of_early_platform_populate(child);
		if (rv) {
			of_node_put(child);
			return rv;
		}
	}

	return 0;
}

/**
 * early_platform_start - start handling early devices
 *
 * This should be called by the architecture code early in the boot sequence
 * to register all early platform drivers, populate the early devices from DT
 * and start matching platform devices specified in machine code.
 */
void early_platform_start(void)
{
	struct early_platform_driver **edrv;
	struct device_node *root;
	int rv;

	WARN_ONCE(!slab_is_available(), "slab is required for early devices\n");

	pr_debug("%s(): registering pending early platform drivers\n",
		 __func__);

	for (edrv = __early_platform_drivers_table;
	     edrv < __early_platform_drivers_table_end; edrv++) {
		rv = early_platform_driver_register(*edrv);
		if (rv)
			pr_warn("error registering early platform driver: %d\n",
				rv);
	}

	if (of_have_populated_dt()) {
		pr_debug("%s(): populating early_platform devices from DT\n",
			 __func__);

		root = of_find_node_by_path("/");

		rv = of_early_platform_populate(root);
		if (rv)
			pr_warn("error populating early devices from DT: %d\n",
				rv);

		of_node_put(root);
	}
}
EXPORT_SYMBOL_GPL(early_platform_start);

/**
 * early_platform_driver_register - register an early platform driver
 * @edrv: early platform driver to register
 *
 * If we're past postcore initcall, this works exactly as
 * platform_device_register().
 */
int early_platform_driver_register(struct early_platform_driver *edrv)
{
	struct early_platform_device *edev;

	if (early_platform_done)
		return platform_driver_register(&edrv->pdrv);

	INIT_LIST_HEAD(&edrv->list);
	list_add_tail(&edrv->list, &early_platform_drivers);

	list_for_each_entry(edev, &early_platform_devices, list) {
		if (platform_match(&edev->pdev.dev, &edrv->pdrv.driver)) {
			early_platform_try_probe(edrv, edev);
			break;
		}
	}

	return 0;
}
EXPORT_SYMBOL_GPL(early_platform_driver_register);

/**
 * early_platform_device_register - register an early platform device
 * @edev: early platform device to register
 *
 * If we're past postcore initcall, this works exactly as
 * platform_device_register().
 */
int early_platform_device_register(struct early_platform_device *edev)
{
	struct early_platform_driver *edrv;

	if (early_platform_done)
		return platform_device_register(&edev->pdev);

	device_initialize(&edev->pdev.dev);
	early_platform_device_add(edev);

	list_for_each_entry(edrv, &early_platform_drivers, list) {
		if (platform_match(&edev->pdev.dev, &edrv->pdrv.driver)) {
			early_platform_try_probe(edrv, edev);
			break;
		}
	}

	return 0;
}
EXPORT_SYMBOL_GPL(early_platform_device_register);

/*
 * This is called once the entire device model infrastructure is in place to
 * seamlessly convert all early platform devices & drivers to regular ones.
 *
 * From this point forward all early platform devices work exactly like normal
 * platform devices.
 */
static int early_platform_finalize(void)
{
	struct early_platform_driver *edrv;
	struct early_platform_device *edev;
	int rv;

	early_platform_done = true;

	pr_debug("%s(): converting early platform drivers to real platform drivers\n",
		 __func__);

	list_for_each_entry(edrv, &early_platform_drivers, list) {
		rv = platform_driver_register(&edrv->pdrv);
		if (rv)
			pr_warn("%s: error converting early platform driver to real platform driver\n",
				edrv->pdrv.driver.name);
	}

	pr_debug("%s(): converting early platform devices to real platform devices\n",
		 __func__);

	list_for_each_entry(edev, &early_platform_devices, list) {
		if (edev->pdev.dev.of_node)
			/* This will be handled by of_platform_populate(). */
			continue;

		kfree(edev->pdev.dev.init_name);

		/*
		 * We don't want to reinitialize the associated struct device
		 * so we must not call platform_device_register().
		 */
		rv = platform_device_add(&edev->pdev);
		if (rv)
			pr_warn("%s: error converting early platform device to real platform device\n",
				dev_name(&edev->pdev.dev));
	}

	return 0;
}
postcore_initcall(early_platform_finalize);
