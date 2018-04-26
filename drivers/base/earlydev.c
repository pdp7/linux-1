// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2018 Texas Instruments
 * Author: Bartosz Golaszewski <bgolaszewski@baylibre.com>
 */

#include <linux/earlydev.h>
#include <linux/slab.h>

#include "base.h"

static bool early_done;
static LIST_HEAD(early_drvs);
static LIST_HEAD(early_devs);

static void earlydev_pdev_set_name(struct platform_device *pdev)
{
	if (pdev->dev.init_name)
		return;

	if (!slab_is_available()) {
		pr_warn("slab unavailable - not assigning name to early device\n");
		return;
	}

	switch (pdev->id) {
		case PLATFORM_DEVID_NONE:
			pdev->dev.init_name = kasprintf(GFP_KERNEL,
							"%s", pdev->name);
			break;
		case PLATFORM_DEVID_AUTO:
			pr_warn("auto device ID not supported in early devices\n");
			break;
		default:
			pdev->dev.init_name = kasprintf(GFP_KERNEL, "%s.%d",
							pdev->name, pdev->id);
			break;
	}

	if (!pdev->dev.init_name)
		pr_warn("error allocating the early device name\n");
}

static void earlydev_probe_devices(void)
{
	struct earlydev_driver *edrv, *ndrv;
	struct earlydev_device *edev, *ndev;
	int rv;

	list_for_each_entry_safe(edev, ndev, &early_devs, list) {
		if (edev->bound_to)
			continue;

		list_for_each_entry_safe(edrv, ndrv, &early_drvs, list) {
			if (strcmp(edrv->plat_drv.driver.name, edev->pdev.name) != 0)
				continue;

			earlydev_pdev_set_name(&edev->pdev);
			rv = edrv->plat_drv.probe(&edev->pdev);
			if (rv) {
				if (rv == -EPROBE_DEFER) {
					/*
					 * Move the device to the end of the
					 * list so that it'll be reprobed next
					 * time after all new devices.
					 */
					list_move_tail(&edev->list,
						       &early_devs);
					continue;
				}

				pr_err("error probing early device: %d\n", rv);
				continue;
			}

			edev->bound_to = edrv;
			edev->pdev.early_probed = true;
		}
	}
}

bool earlydev_probing_early(void)
{
	return !early_done;
}

bool earlydev_probe_late(struct platform_device *pdev)
{
	struct earlydev_device *edev;

	edev = container_of(pdev, struct earlydev_device, pdev);

	return edev->probe_late;
}

void __earlydev_driver_register(struct earlydev_driver *edrv,
				struct module *owner)
{
	if (early_done) {
		__platform_driver_register(&edrv->plat_drv, owner);
		return;
	}

	pr_debug("registering early driver: %s\n", edrv->plat_drv.driver.name);

	edrv->plat_drv.driver.owner = owner;

	INIT_LIST_HEAD(&edrv->list);
	list_add(&early_drvs, &edrv->list);

	earlydev_probe_devices();
}
EXPORT_SYMBOL_GPL(__earlydev_driver_register);

void earlydev_device_add(struct earlydev_device *edev)
{
	if (early_done) {
		platform_device_register(&edev->pdev);
		return;
	}

	pr_debug("adding early device: %s\n", edev->pdev.name);

	INIT_LIST_HEAD(&edev->list);
	list_add(&early_devs, &edev->list);

	earlydev_probe_devices();
}
EXPORT_SYMBOL_GPL(earlydev_device_add);

static void earlydev_drivers_to_platform(void)
{
	struct earlydev_driver *edrv, *n;
	struct platform_driver *pdrv;
	int rv;

	list_for_each_entry_safe(edrv, n, &early_drvs, list) {
		pdrv = &edrv->plat_drv;

		rv = __platform_driver_register(pdrv, pdrv->driver.owner);
		if (rv)
			pr_err("error switching early to platform driver: %d\n",
			       rv);

		list_del(&edrv->list);
	}
}

static void earlydev_devices_to_platform(void)
{
	struct earlydev_device *edev, *n;
	struct platform_device *pdev;
	int rv;

	list_for_each_entry_safe(edev, n, &early_devs, list) {
		pdev = &edev->pdev;

		rv = platform_device_register(pdev);
		if (rv)
			pr_err("error switching early to platform device: %d\n",
			       rv);
	}
}

static int earlydev_switch_to_platform(void)
{
	pr_debug("switching early drivers and devices to platform\n");
	early_done = true;

	earlydev_drivers_to_platform();
	earlydev_devices_to_platform();

	return 0;
}
device_initcall(earlydev_switch_to_platform);
