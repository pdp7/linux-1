// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2018 Texas Instruments
 *
 * Author:
 *   Bartosz Golaszewski <bgolaszewski@baylibre.com>
 *
 * Dummy testing driver whose only purpose is to be registered and probed
 * using the early platform device mechanism.
 */

#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_device.h>
#include <linux/platform_device.h>
#include <linux/early_platform.h>

struct dummy_early_data {
	int a;
	int b;
};

static int dummy_early_probe_early(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct dummy_early_data *data;
	struct resource *res;

	dev_notice(dev, "dummy-early driver probed early!\n");

	data = devm_kzalloc(dev, sizeof(*data), GFP_KERNEL);
	if (!data)
		return -ENOMEM;

	data->a = 123;
	data->b = 321;

	dev_dbg(dev, "setting driver data early: a = %d, b = %d\n",
		data->a, data->b);
	dev_set_drvdata(dev, data);

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (res)
		dev_dbg(dev, "got early resource: start = 0x%08x, end = 0x%08x\n",
			res->start, res->end);

	return 0;
}

static int dummy_early_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct dummy_early_data *data;

	dev_notice(dev, "dummy-early driver probed late!\n");
	data = dev_get_drvdata(dev);
	dev_dbg(dev, "retrieving driver data late: a = %d, b = %d\n",
		data->a, data->b);

	return 0;
}

static const struct of_device_id dummy_early_of_match[] = {
	{ .compatible = "dummy-early", },
	{ },
};

static struct early_platform_driver dummy_early_driver = {
	.early_probe = dummy_early_probe_early,
	.pdrv = {
		.probe = dummy_early_probe,
		.driver = {
			.name = "dummy-early",
			.of_match_table = dummy_early_of_match,
		},
	}
};
module_early_platform_driver(dummy_early_driver);

MODULE_AUTHOR("Bartosz Golaszewski <bgolaszewski@baylibre.com>");
MODULE_DESCRIPTION("Dummy early platform device driver");
MODULE_LICENSE("GPL v2");
