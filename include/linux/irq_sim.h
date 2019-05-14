/* SPDX-License-Identifier: GPL-2.0+ */
/*
 * Copyright (C) 2017-2018 Bartosz Golaszewski <brgl@bgdev.pl>
 */

#ifndef _LINUX_IRQ_SIM_H
#define _LINUX_IRQ_SIM_H

#include <linux/irq_work.h>
#include <linux/device.h>

/*
 * Provides a framework for allocating simulated interrupts which can be
 * requested like normal irqs and enqueued from process context.
 */

struct irq_sim;

struct irq_sim *irq_sim_new(unsigned int num_irqs);
struct irq_sim *devm_irq_sim_new(struct device *dev, unsigned int num_irqs);
void irq_sim_free(struct irq_sim *sim);
void irq_sim_fire(struct irq_sim *sim, unsigned int offset);
int irq_sim_irqnum(struct irq_sim *sim, unsigned int offset);

#endif /* _LINUX_IRQ_SIM_H */
