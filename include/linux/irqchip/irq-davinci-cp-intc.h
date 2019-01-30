/* SPDX-License-Identifier: GPL-2.0+ */
/*
 * Copyright (C) 2019 Texas Instruments
 */

#ifndef _LINUX_IRQ_DAVINCI_CP_INTC_
#define _LINUX_IRQ_DAVINCI_CP_INTC_

#include <linux/ioport.h>

struct davinci_cp_intc_config {
	struct resource reg;
	unsigned int num_irqs;
};

int davinci_cp_intc_init(const struct davinci_cp_intc_config *config);

#endif /* _LINUX_IRQ_DAVINCI_CP_INTC_ */
