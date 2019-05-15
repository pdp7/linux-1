// SPDX-License-Identifier: GPL-2.0-only
//
// TI DaVinci clocksource driver
//
// Copyright (C) 2019 Texas Instruments
// Author: Bartosz Golaszewski <bgolaszewski@baylibre.com>
// (with tiny parts adopted from code by Kevin Hilman <khilman@baylibre.com>)

#include <linux/clk.h>
#include <linux/clockchips.h>
#include <linux/interrupt.h>
#include <linux/kernel.h>
#include <linux/of_address.h>
#include <linux/of_irq.h>
#include <linux/sched_clock.h>

#include <clocksource/timer-davinci.h>

#undef pr_fmt
#define pr_fmt(fmt) "%s: " fmt "\n", __func__

#define DAVINCI_TIMER_REG_TIM12			0x10
#define DAVINCI_TIMER_REG_TIM34			0x14
#define DAVINCI_TIMER_REG_PRD12			0x18
#define DAVINCI_TIMER_REG_PRD34			0x1c
#define DAVINCI_TIMER_REG_TCR			0x20
#define DAVINCI_TIMER_REG_TGCR			0x24

#define DAVINCI_TIMER_TIMMODE_MASK		GENMASK(3, 2)
#define DAVINCI_TIMER_RESET_MASK		GENMASK(1, 0)
#define DAVINCI_TIMER_TIMMODE_32BIT_UNCHAINED	BIT(2)
#define DAVINCI_TIMER_UNRESET			GENMASK(1, 0)

#define DAVINCI_TIMER_ENAMODE_MASK		GENMASK(1, 0)
#define DAVINCI_TIMER_ENAMODE_DISABLED		0x00
#define DAVINCI_TIMER_ENAMODE_ONESHOT		BIT(0)
#define DAVINCI_TIMER_ENAMODE_PERIODIC		BIT(1)

#define DAVINCI_TIMER_ENAMODE_SHIFT_TIM12	6
#define DAVINCI_TIMER_ENAMODE_SHIFT_TIM34	22

#define DAVINCI_TIMER_MIN_DELTA			0x01
#define DAVINCI_TIMER_MAX_DELTA			0xfffffffe

#define DAVINCI_TIMER_CLKSRC_BITS		32

#define DAVINCI_TIMER_TGCR_DEFAULT \
		(DAVINCI_TIMER_TIMMODE_32BIT_UNCHAINED | DAVINCI_TIMER_UNRESET)

struct davinci_clockevent {
	struct clock_event_device dev;
	void __iomem *base;

	unsigned int tim_off;
	unsigned int prd_off;
	unsigned int cmp_off;

	unsigned int enamode_disabled;
	unsigned int enamode_oneshot;
	unsigned int enamode_periodic;
	unsigned int enamode_mask;
};

/*
 * This must be globally accessible by davinci_timer_read_sched_clock(), so
 * let's keep it here.
 */
static struct {
	struct clocksource dev;
	void __iomem *base;
	unsigned int tim_off;
} davinci_clocksource;

static struct davinci_clockevent *
to_davinci_clockevent(struct clock_event_device *clockevent)
{
	return container_of(clockevent, struct davinci_clockevent, dev);
}

static unsigned int
davinci_clockevent_read(struct davinci_clockevent *clockevent,
			unsigned int reg)
{
	return readl_relaxed(clockevent->base + reg);
}

static void davinci_clockevent_write(struct davinci_clockevent *clockevent,
				     unsigned int reg, unsigned int val)
{
	writel_relaxed(val, clockevent->base + reg);
}

static void davinci_reg_update(void __iomem *base, unsigned int reg,
			       unsigned int mask, unsigned int val)
{
	unsigned int new, orig;

	orig = readl_relaxed(base + reg);
	new = orig & ~mask;
	new |= val & mask;

	writel_relaxed(new, base + reg);
}

static void davinci_clockevent_update(struct davinci_clockevent *clockevent,
				      unsigned int reg, unsigned int mask,
				      unsigned int val)
{
	davinci_reg_update(clockevent->base, reg, mask, val);
}

static int
davinci_clockevent_set_next_event_std(unsigned long cycles,
				      struct clock_event_device *dev)
{
	struct davinci_clockevent *clockevent;
	unsigned int enamode;

	clockevent = to_davinci_clockevent(dev);
	enamode = clockevent->enamode_disabled;

	davinci_clockevent_update(clockevent, DAVINCI_TIMER_REG_TCR,
				  clockevent->enamode_mask,
				  clockevent->enamode_disabled);

	davinci_clockevent_write(clockevent, clockevent->tim_off, 0x0);
	davinci_clockevent_write(clockevent, clockevent->prd_off, cycles);

	if (clockevent_state_oneshot(&clockevent->dev))
		enamode = clockevent->enamode_oneshot;
	else if (clockevent_state_periodic(&clockevent->dev))
		enamode = clockevent->enamode_periodic;

	davinci_clockevent_update(clockevent, DAVINCI_TIMER_REG_TCR,
				  clockevent->enamode_mask, enamode);

	return 0;
}

static int
davinci_clockevent_set_next_event_cmp(unsigned long cycles,
				      struct clock_event_device *dev)
{
	struct davinci_clockevent *clockevent = to_davinci_clockevent(dev);
	unsigned int curr_time;

	curr_time = davinci_clockevent_read(clockevent, clockevent->tim_off);
	davinci_clockevent_write(clockevent,
				 clockevent->cmp_off, curr_time + cycles);

	return 0;
}

static irqreturn_t davinci_timer_irq_timer(int irq, void *data)
{
	struct davinci_clockevent *clockevent = data;

	clockevent->dev.event_handler(&clockevent->dev);

	return IRQ_HANDLED;
}

static u64 notrace davinci_timer_read_sched_clock(void)
{
	return readl_relaxed(davinci_clocksource.base +
			     davinci_clocksource.tim_off);
}

static u64 davinci_clocksource_read(struct clocksource *dev)
{
	return davinci_timer_read_sched_clock();
}

static void davinci_clocksource_init(void __iomem *base, unsigned int tim_off,
				     unsigned int prd_off, unsigned int shift)
{
	davinci_reg_update(base, DAVINCI_TIMER_REG_TCR,
			   DAVINCI_TIMER_ENAMODE_MASK << shift,
			   DAVINCI_TIMER_ENAMODE_DISABLED << shift);

	writel_relaxed(0x0, base + tim_off);
	writel_relaxed(UINT_MAX, base + prd_off);

	davinci_reg_update(base, DAVINCI_TIMER_REG_TCR,
			   DAVINCI_TIMER_ENAMODE_MASK << shift,
			   DAVINCI_TIMER_ENAMODE_PERIODIC << shift);
}

static void davinci_timer_init(void __iomem *base)
{
	/* Set clock to internal mode and disable it. */
	writel_relaxed(0x0, base + DAVINCI_TIMER_REG_TCR);
	/*
	 * Reset both 32-bit timers, set no prescaler for timer 34, set the
	 * timer to dual 32-bit unchained mode, unreset both 32-bit timers.
	 */
	writel_relaxed(DAVINCI_TIMER_TGCR_DEFAULT,
		       base + DAVINCI_TIMER_REG_TGCR);
	/* Init both counters to zero. */
	writel_relaxed(0x0, base + DAVINCI_TIMER_REG_TIM12);
	writel_relaxed(0x0, base + DAVINCI_TIMER_REG_TIM34);
}

int __init davinci_timer_register(struct clk *clk,
				  const struct davinci_timer_cfg *timer_cfg)
{
	struct davinci_clockevent *clockevent;
	unsigned int tick_rate, shift;
	void __iomem *base;
	int rv;

	rv = clk_prepare_enable(clk);
	if (rv) {
		pr_err("Unable to prepare and enable the timer clock");
		return rv;
	}

	base = request_mem_region(timer_cfg->reg.start,
				  resource_size(&timer_cfg->reg),
				  "davinci-timer");
	if (!base) {
		pr_err("Unable to request memory region");
		return -EBUSY;
	}

	base = ioremap(timer_cfg->reg.start, resource_size(&timer_cfg->reg));
	if (!base) {
		pr_err("Unable to map the register range");
		return -ENOMEM;
	}

	davinci_timer_init(base);
	tick_rate = clk_get_rate(clk);

	clockevent = kzalloc(sizeof(*clockevent), GFP_KERNEL);
	if (!clockevent) {
		pr_err("Error allocating memory for clockevent data");
		return -ENOMEM;
	}

	clockevent->dev.name = "tim12";
	clockevent->dev.features = CLOCK_EVT_FEAT_ONESHOT;
	clockevent->dev.cpumask = cpumask_of(0);

	clockevent->base = base;
	clockevent->tim_off = DAVINCI_TIMER_REG_TIM12;
	clockevent->prd_off = DAVINCI_TIMER_REG_PRD12;

	shift = DAVINCI_TIMER_ENAMODE_SHIFT_TIM12;
	clockevent->enamode_disabled = DAVINCI_TIMER_ENAMODE_DISABLED << shift;
	clockevent->enamode_oneshot = DAVINCI_TIMER_ENAMODE_ONESHOT << shift;
	clockevent->enamode_periodic = DAVINCI_TIMER_ENAMODE_PERIODIC << shift;
	clockevent->enamode_mask = DAVINCI_TIMER_ENAMODE_MASK << shift;

	if (timer_cfg->cmp_off) {
		clockevent->cmp_off = timer_cfg->cmp_off;
		clockevent->dev.set_next_event =
				davinci_clockevent_set_next_event_cmp;
	} else {
		clockevent->dev.set_next_event =
				davinci_clockevent_set_next_event_std;
	}

	rv = request_irq(timer_cfg->irq[DAVINCI_TIMER_CLOCKEVENT_IRQ].start,
			 davinci_timer_irq_timer, IRQF_TIMER,
			 "clockevent", clockevent);
	if (rv) {
		pr_err("Unable to request the clockevent interrupt");
		return rv;
	}

	clockevents_config_and_register(&clockevent->dev, tick_rate,
					DAVINCI_TIMER_MIN_DELTA,
					DAVINCI_TIMER_MAX_DELTA);

	davinci_clocksource.dev.rating = 300;
	davinci_clocksource.dev.read = davinci_clocksource_read;
	davinci_clocksource.dev.mask =
			CLOCKSOURCE_MASK(DAVINCI_TIMER_CLKSRC_BITS);
	davinci_clocksource.dev.flags = CLOCK_SOURCE_IS_CONTINUOUS;
	davinci_clocksource.base = base;

	if (timer_cfg->cmp_off) {
		davinci_clocksource.dev.name = "tim12";
		davinci_clocksource.tim_off = DAVINCI_TIMER_REG_TIM12;
		davinci_clocksource_init(base,
					 DAVINCI_TIMER_REG_TIM12,
					 DAVINCI_TIMER_REG_PRD12,
					 DAVINCI_TIMER_ENAMODE_SHIFT_TIM12);
	} else {
		davinci_clocksource.dev.name = "tim34";
		davinci_clocksource.tim_off = DAVINCI_TIMER_REG_TIM34;
		davinci_clocksource_init(base,
					 DAVINCI_TIMER_REG_TIM34,
					 DAVINCI_TIMER_REG_PRD34,
					 DAVINCI_TIMER_ENAMODE_SHIFT_TIM34);
	}

	rv = clocksource_register_hz(&davinci_clocksource.dev, tick_rate);
	if (rv) {
		pr_err("Unable to register clocksource");
		return rv;
	}

	sched_clock_register(davinci_timer_read_sched_clock,
			     DAVINCI_TIMER_CLKSRC_BITS, tick_rate);

	return 0;
}

static int __init of_davinci_timer_register(struct device_node *np)
{
	struct davinci_timer_cfg timer_cfg = { };
	struct clk *clk;
	int rv;

	rv = of_address_to_resource(np, 0, &timer_cfg.reg);
	if (rv) {
		pr_err("Unable to get the register range for timer");
		return rv;
	}

	rv = of_irq_to_resource_table(np, timer_cfg.irq,
				      DAVINCI_TIMER_NUM_IRQS);
	if (rv != DAVINCI_TIMER_NUM_IRQS) {
		pr_err("Unable to get the interrupts for timer");
		return rv;
	}

	clk = of_clk_get(np, 0);
	if (IS_ERR(clk)) {
		pr_err("Unable to get the timer clock");
		return PTR_ERR(clk);
	}

	rv = davinci_timer_register(clk, &timer_cfg);
	if (rv)
		clk_put(clk);

	return rv;
}
TIMER_OF_DECLARE(davinci_timer, "ti,da830-timer", of_davinci_timer_register);
