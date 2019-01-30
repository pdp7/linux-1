/* SPDX-License-Identifier: GPL-2.0+ */
/*
 * Copyright (C) 2019 Texas Instruments
 */

#ifndef _LINUX_IRQ_DAVINCI_AINTC_
#define _LINUX_IRQ_DAVINCI_AINTC_

#include <linux/ioport.h>

struct davinci_aintc_config {
	struct resource reg;
	unsigned int num_irqs;
	u8 *prios;
};

#endif /* _LINUX_IRQ_DAVINCI_AINTC_ */
