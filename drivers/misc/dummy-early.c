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

static int dummy_early_probe(struct platform_device *pdev)
{
	dev_notice(&pdev->dev, "dummy-early driver probed!\n");

	return 0;
}

static const struct of_device_id dummy_early_of_match[] = {
	{ .compatible = "none,dummy-early", },
	{ },
};

static struct platform_driver dummy_early_driver = {
	.probe = dummy_early_probe,
	.driver = {
		.name = "dummy-early",
		.of_match_table = dummy_early_of_match,
	},
};
early_platform_driver(dummy_early_driver);

MODULE_AUTHOR("Bartosz Golaszewski <bgolaszewski@baylibre.com>");
MODULE_DESCRIPTION("Dummy early platform device driver");
MODULE_LICENSE("GPL v2");
