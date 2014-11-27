/*
 *  TI EDMA definitions
 *
 *  Copyright (C) 2006-2013 Texas Instruments.
 *
 *  This program is free software; you can redistribute  it and/or modify it
 *  under  the terms of  the GNU General  Public License as published by the
 *  Free Software Foundation;  either version 2 of the  License, or (at your
 *  option) any later version.
 */
#ifndef EDMA_H_
#define EDMA_H_

#define EDMA_MAX_CC	2

enum dma_event_q {
	EVENTQ_0 = 0,
	EVENTQ_1 = 1,
	EVENTQ_2 = 2,
	EVENTQ_3 = 3,
	EVENTQ_DEFAULT = -1
};

struct edma_rsv_info {
	const s16 (*rsv_chans)[2];
	const s16 (*rsv_slots)[2];
};

/* platform_data for EDMA driver */
struct edma_soc_info {
	/*
	 * Default queue is expected to be a low-priority queue.
	 * This way, long transfers on the default queue started
	 * by the codec engine will not cause audio defects.
	 */
	enum dma_event_q default_queue;

	/* Resource reservation for other cores */
	struct edma_rsv_info *rsv;

	s8 (*queue_priority_mapping)[2];
	const s16 (*xbar_chans)[2];
};

#endif
