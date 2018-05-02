/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (C) 2018 Texas Instruments, Inc.
 *
 * Author:
 *     Bartosz Golaszewski <bgolaszewski@baylibre.com>
 */

#ifndef __EARLY_PLATFORM_H__
#define __EARLY_PLATFORM_H__

#include <linux/platform_device.h>
#include <linux/types.h>

/**
 * struct early_platform_driver
 *
 * @pdrv: real platform driver associated with this early platform driver
 * @list: list head for the list of early platform drivers
 * @early_probe: early probe callback
 */
struct early_platform_driver {
	struct platform_driver pdrv;
	struct list_head list;
	int (*early_probe)(struct platform_device *);
};

/**
 * struct early_platform_device
 *
 * @pdev: real platform device associated with this early platform device
 * @list: list head for the list of early platform devices
 * @deferred: true if this device's early probe was deferred
 * @deferred_drv: early platform driver with which this device was matched
 */
struct early_platform_device {
	struct platform_device pdev;
	struct list_head list;
	bool deferred;
	struct early_platform_driver *deferred_drv;
};

#ifdef CONFIG_EARLY_PLATFORM
extern void early_platform_start(void);
extern int early_platform_driver_register(struct early_platform_driver *edrv);
extern int early_platform_device_register(struct early_platform_device *edev);
#else /* CONFIG_EARLY_PLATFORM */
static inline void early_platform_start(void) {}
static inline int
early_platform_driver_register(struct early_platform_driver *edrv)
{
	return -ENOSYS;
}
static inline int
early_platform_device_register(struct early_platform_device *edev)
{
	return -ENOSYS;
}
#endif /* CONFIG_EARLY_PLATFORM */

#if defined(CONFIG_EARLY_PLATFORM) && defined(CONFIG_OF)
extern struct platform_device *
of_early_to_platform_device(struct device_node *np);
#else
static inline struct platform_device *
of_early_to_platform_device(struct device_node *np)
{
	return ERR_PTR(-ENOSYS);
}
#endif /* defined(CONFIG_EARLY_PLATFORM) && defined(CONFIG_OF) */

#ifdef CONFIG_EARLY_PLATFORM
#define module_early_platform_driver(_edrv)				\
	static const struct early_platform_driver *__##_edrv##_entry	\
		__used __section(__early_platform_drivers_table)	\
		= &(_edrv)
#else /* CONFIG_EARLY_PLATFORM */
#define module_early_platform_driver(_edrv)
#endif /* CONFIG_EARLY_PLATFORM */

#endif /* __EARLY_PLATFORM_H__ */
