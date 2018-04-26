/* SPDXSPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (C) 2018 Texast Instruments
 * Author: Bartosz Golaszewski <bgolaszewski@baylibre.com>
 */

#ifndef __EARLYDEV_H__
#define __EARLYDEV_H__

#include <linux/types.h>
#include <linux/init.h>
#include <linux/device.h>
#include <linux/platform_device.h>

struct earlydev_driver {
	struct list_head list;
	struct platform_driver plat_drv;
};

struct earlydev_device {
	struct list_head list;
	struct platform_device pdev;
	struct earlydev_driver *bound_to;
	bool probe_late;
};

#ifdef CONFIG_EARLYDEV
extern bool earlydev_probing_early(void);
extern bool earlydev_probe_late(struct platform_device *pdev);
extern void __earlydev_driver_register(struct earlydev_driver *edrv,
				       struct module *owner);
#define earlydev_driver_register(_driver) \
	__earlydev_driver_register((_driver), THIS_MODULE)
extern void earlydev_device_add(struct earlydev_device *edev);
#else /* CONFIG_EARLYDEV */
static inline void earlydev_probing_early(void)
{
	return false;
}
static inline bool earlydev_probe_late(struct platform_device *pdev)
{
	return true;
}
static inline void __earlydev_driver_register(struct earlydev_driver *drv,
					       struct module *owner) {}
#define earlydev_driver_register(_driver)
static inline void earlydev_device_add(struct earlydev_device *edev) {}
#endif /* CONFIG_EARLYDEV */

/*
 * REVISIT: early_initcall may be still too late for some timers and critical
 * clocks. We should probably have a separate section with callbacks that can
 * be invoked at each architecture's discretion.
 */
#define earlydev_platform_driver(_drv)					\
	static int _drv##_register(void)				\
	{								\
		earlydev_driver_register(&(_drv));			\
		return 0;						\
	}								\
	early_initcall(_drv##_register)

#endif /* __EARLYDEV_H__ */
