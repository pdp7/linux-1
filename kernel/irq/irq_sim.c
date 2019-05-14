// SPDX-License-Identifier: GPL-2.0+
/*
 * Copyright (C) 2017-2018 Bartosz Golaszewski <brgl@bgdev.pl>
 */

#include <linux/slab.h>
#include <linux/irq_sim.h>
#include <linux/irq.h>

struct irq_sim_work_ctx {
	struct irq_work		work;
	unsigned long		*pending;
};

struct irq_sim_irq_ctx {
	int			irqnum;
	bool			enabled;
};

struct irq_sim {
	struct irq_sim_work_ctx	work_ctx;
	int			irq_base;
	unsigned int		irq_count;
	struct irq_sim_irq_ctx	*irqs;
};

struct irq_sim_devres {
	struct irq_sim		*sim;
};

static void irq_sim_irqmask(struct irq_data *data)
{
	struct irq_sim_irq_ctx *irq_ctx = irq_data_get_irq_chip_data(data);

	irq_ctx->enabled = false;
}

static void irq_sim_irqunmask(struct irq_data *data)
{
	struct irq_sim_irq_ctx *irq_ctx = irq_data_get_irq_chip_data(data);

	irq_ctx->enabled = true;
}

static int irq_sim_set_type(struct irq_data *data, unsigned int type)
{
	/* We only support rising and falling edge trigger types. */
	if (type & ~IRQ_TYPE_EDGE_BOTH)
		return -EINVAL;

	irqd_set_trigger_type(data, type);

	return 0;
}

static struct irq_chip irq_sim_irqchip = {
	.name		= "irq_sim",
	.irq_mask	= irq_sim_irqmask,
	.irq_unmask	= irq_sim_irqunmask,
	.irq_set_type	= irq_sim_set_type,
};

static void irq_sim_handle_irq(struct irq_work *work)
{
	struct irq_sim_work_ctx *work_ctx;
	unsigned int offset = 0;
	struct irq_sim *sim;
	int irqnum;

	work_ctx = container_of(work, struct irq_sim_work_ctx, work);
	sim = container_of(work_ctx, struct irq_sim, work_ctx);

	while (!bitmap_empty(work_ctx->pending, sim->irq_count)) {
		offset = find_next_bit(work_ctx->pending,
				       sim->irq_count, offset);
		clear_bit(offset, work_ctx->pending);
		irqnum = irq_sim_irqnum(sim, offset);
		handle_simple_irq(irq_to_desc(irqnum));
	}
}

/**
 * irq_sim_new - Create a new interrupt simulator: allocate a range of
 *               dummy interrupts.
 *
 * @num_irqs:   Number of interrupts to allocate
 *
 * On success: return the new irq_sim object.
 * On failure: a negative errno wrapped with ERR_PTR().
 */
struct irq_sim *irq_sim_new(unsigned int num_irqs)
{
	struct irq_sim *sim;
	int i;

	sim = kmalloc(sizeof(*sim), GFP_KERNEL);
	if (!sim)
		return ERR_PTR(-ENOMEM);

	sim->irqs = kmalloc_array(num_irqs, sizeof(*sim->irqs), GFP_KERNEL);
	if (!sim->irqs) {
		kfree(sim);
		return ERR_PTR(-ENOMEM);
	}

	sim->irq_base = irq_alloc_descs(-1, 0, num_irqs, 0);
	if (sim->irq_base < 0) {
		kfree(sim->irqs);
		kfree(sim);
		return ERR_PTR(sim->irq_base);
	}

	sim->work_ctx.pending = bitmap_zalloc(num_irqs, GFP_KERNEL);
	if (!sim->work_ctx.pending) {
		kfree(sim->irqs);
		kfree(sim);
		irq_free_descs(sim->irq_base, num_irqs);
		return ERR_PTR(-ENOMEM);
	}

	for (i = 0; i < num_irqs; i++) {
		sim->irqs[i].irqnum = sim->irq_base + i;
		sim->irqs[i].enabled = false;
		irq_set_chip(sim->irq_base + i, &irq_sim_irqchip);
		irq_set_chip_data(sim->irq_base + i, &sim->irqs[i]);
		irq_set_handler(sim->irq_base + i, &handle_simple_irq);
		irq_modify_status(sim->irq_base + i,
				  IRQ_NOREQUEST | IRQ_NOAUTOEN, IRQ_NOPROBE);
	}

	init_irq_work(&sim->work_ctx.work, irq_sim_handle_irq);
	sim->irq_count = num_irqs;

	return sim;
}
EXPORT_SYMBOL_GPL(irq_sim_new);

/**
 * irq_sim_free - Deinitialize the interrupt simulator: free the interrupt
 *                descriptors and allocated memory.
 *
 * @sim:        The interrupt simulator to tear down.
 */
void irq_sim_free(struct irq_sim *sim)
{
	irq_work_sync(&sim->work_ctx.work);
	bitmap_free(sim->work_ctx.pending);
	irq_free_descs(sim->irq_base, sim->irq_count);
	kfree(sim->irqs);
	kfree(sim);
}
EXPORT_SYMBOL_GPL(irq_sim_free);

static void devm_irq_sim_release(struct device *dev, void *res)
{
	struct irq_sim_devres *this = res;

	irq_sim_free(this->sim);
}

/**
 * devm_irq_sim_new - Create a new interrupt simulator for a managed device.
 *
 * @dev:        Device to initialize the simulator object for.
 * @num_irqs:   Number of interrupts to allocate
 *
 * On success: return a new irq_sim object.
 * On failure: a negative errno wrapped with ERR_PTR().
 */
struct irq_sim *devm_irq_sim_new(struct device *dev, unsigned int num_irqs)
{
	struct irq_sim_devres *dr;

	dr = devres_alloc(devm_irq_sim_release, sizeof(*dr), GFP_KERNEL);
	if (!dr)
		return ERR_PTR(-ENOMEM);

	dr->sim = irq_sim_new(num_irqs);
	if (IS_ERR(dr->sim)) {
		devres_free(dr);
		return dr->sim;
	}

	devres_add(dev, dr);
	return dr->sim;
}
EXPORT_SYMBOL_GPL(devm_irq_sim_new);

/**
 * irq_sim_fire - Enqueue an interrupt.
 *
 * @sim:        The interrupt simulator object.
 * @offset:     Offset of the simulated interrupt which should be fired.
 */
void irq_sim_fire(struct irq_sim *sim, unsigned int offset)
{
	if (sim->irqs[offset].enabled) {
		set_bit(offset, sim->work_ctx.pending);
		irq_work_queue(&sim->work_ctx.work);
	}
}
EXPORT_SYMBOL_GPL(irq_sim_fire);

/**
 * irq_sim_irqnum - Get the allocated number of a dummy interrupt.
 *
 * @sim:        The interrupt simulator object.
 * @offset:     Offset of the simulated interrupt for which to retrieve
 *              the number.
 */
int irq_sim_irqnum(struct irq_sim *sim, unsigned int offset)
{
	return sim->irqs[offset].irqnum;
}
EXPORT_SYMBOL_GPL(irq_sim_irqnum);
