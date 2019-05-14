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
	struct irq_sim_work_ctx	*work_ctx;
};

struct irq_sim {
	struct irq_sim_work_ctx	work_ctx;
	int			irq_base;
	unsigned int		irq_count;
	struct irq_domain	*domain;
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
		irqnum = irq_find_mapping(sim->domain, offset);
		handle_simple_irq(irq_to_desc(irqnum));
	}
}

static int irq_sim_domain_map(struct irq_domain *domain,
			      unsigned int virq, irq_hw_number_t hw)
{
	struct irq_sim *sim = domain->host_data;
	struct irq_sim_irq_ctx *ctx;

	ctx = kzalloc(sizeof(*ctx), GFP_KERNEL);
	if (!ctx)
		return -ENOMEM;

	irq_set_chip(virq, &irq_sim_irqchip);
	irq_set_chip_data(virq, ctx);
	irq_set_handler(virq, handle_simple_irq);
	irq_modify_status(virq, IRQ_NOREQUEST | IRQ_NOAUTOEN, IRQ_NOPROBE);
	ctx->work_ctx = &sim->work_ctx;

	return 0;
}

static void irq_sim_domain_unmap(struct irq_domain *domain, unsigned int virq)
{
	struct irq_sim_irq_ctx *ctx;
	struct irq_data *irqd;

	irqd = irq_domain_get_irq_data(domain, virq);
	ctx = irq_data_get_irq_chip_data(irqd);

	kfree(ctx);
}

static const struct irq_domain_ops irq_sim_domain_ops = {
	.map		= irq_sim_domain_map,
	.unmap		= irq_sim_domain_unmap,
};

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

	sim = kmalloc(sizeof(*sim), GFP_KERNEL);
	if (!sim)
		return ERR_PTR(-ENOMEM);

	sim->work_ctx.pending = bitmap_zalloc(num_irqs, GFP_KERNEL);
	if (!sim->work_ctx.pending) {
		kfree(sim);
		return ERR_PTR(-ENOMEM);
	}

	sim->domain = irq_domain_create_linear(NULL, num_irqs,
					       &irq_sim_domain_ops, sim);
	if (!sim->domain)
		return ERR_PTR(-ENOMEM);

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
	int i, irq;

	for (i = 0; i < sim->irq_count; i++) {
		irq = irq_find_mapping(sim->domain, i);
		if (irq)
			irq_dispose_mapping(irq);
	}

	irq_domain_remove(sim->domain);
	irq_work_sync(&sim->work_ctx.work);
	bitmap_free(sim->work_ctx.pending);
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
 * @virq:        Virtual interrupt number to fire. It must be associated with
 *               an existing interrupt simulator.
 */
void irq_sim_fire(int virq)
{
	struct irq_sim_irq_ctx *ctx;
	struct irq_data *irqd;

	irqd = irq_get_irq_data(virq);
	ctx = irq_data_get_irq_chip_data(irqd);

	if (ctx->enabled) {
		set_bit(irqd_to_hwirq(irqd), ctx->work_ctx->pending);
		irq_work_queue(&ctx->work_ctx->work);
	}
}
EXPORT_SYMBOL_GPL(irq_sim_fire);

/**
 * irq_sim_get_domain - Retrieve the interrupt domain for this simulator.
 *
 * @sim:         The interrupt simulator the domain of which we retrieve.
 */
struct irq_domain *irq_sim_get_domain(struct irq_sim *sim)
{
	return sim->domain;
}
EXPORT_SYMBOL(irq_sim_get_domain);
