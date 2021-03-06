/***************************************************************
  Jiguo: This is the header file to help the kernel keep clean 
         for recovery related changes
         depends on gcc will optimize away for i = 0 case
***************************************************************/

#ifndef RECOVERY_H
#define RECOVERY_H

#include "spd.h"
#include "thread.h"

//#define MEASURE_COST

#ifdef MEASURE_COST
#define MEAS_INV_FAULT_DETECT   /* measure the fault detection cost for invocation */
#define MEAS_RET_FAULT_DETECT   /* measure the fault detection cost for return */
#define MEAS_TCS_FAULT_DETECT	/* measure the fault detection cost for thread context switch */
#define MEAS_INT_FAULT_DETECT   /* measure the fault detection cost for interrupt */
#endif

#ifdef RECOVERY_ENABLE
//*************************************************
/* enable recovery */
//*************************************************

/*---------Fault Notification Operations--------*/
static inline int
init_spd_fault_cnt(struct spd *spd)
{
	spd->fault.cnt = 0;
	return 0;
}

static inline int
init_cap_fault_cnt(struct invocation_cap *cap)
{
	cap->fault.cnt = 0;
	return 0;
}

static inline int
init_invframe_fault_cnt(struct thd_invocation_frame *inv_frame)
{
	inv_frame->fault.cnt = 0;
	inv_frame->curr_fault.cnt = 0;
	return 0;
}

/**************  detect ************/
static inline int
ipc_fault_detect(struct invocation_cap *cap_entry, struct spd *dest_spd)
{
	if (cap_entry->fault.cnt != dest_spd->fault.cnt) {
		/* printk("dest spd %d fault cnt %lu\n", spd_get_index(dest_spd), dest_spd->fault.cnt); */
		return 1;
	}
	else return 0;
}

static inline int
pop_fault_detect(struct thd_invocation_frame *inv_frame, struct thd_invocation_frame *curr_frame)
{
	if (inv_frame->fault.cnt != curr_frame->spd->fault.cnt) return 1;
	else return 0;
}

static inline int
switch_thd_fault_detect(struct thread *next)
{
	struct spd *n_spd;
	struct thd_invocation_frame *tif;

	tif    = thd_invstk_top(next);
	n_spd  = tif->spd;

	if (tif->curr_fault.cnt != n_spd->fault.cnt) 
	{
		/* printk("thread %d curr_fault cnt %d\n", thd_get_id(next), tif->curr_fault.cnt); */
		/* printk("spd %d fault cnt %d\n", spd_get_index(n_spd), n_spd->fault.cnt); */
		return 1;
	}
	else return 0;
}

static inline int
interrupt_fault_detect(struct thread *next) /* for now, this is the timer thread */
{
	struct spd *n_spd;
	struct thd_invocation_frame *tif;

	tif    = thd_invstk_top(next);
	n_spd  = tif->spd;
	
	if (tif->curr_fault.cnt != n_spd->fault.cnt) 
	{
		/* printk("thread %d fault cnt %d\n", thd_get_id(next), tif->fault.cnt); */
		/* printk("spd %d fault cnt %d\n", spd_get_index(n_spd), n_spd->fault.cnt); */
		return 1;
	}
	else return 0;
}
/**************  update ************/
static inline int
inv_frame_fault_cnt_update(struct thread *thd, struct spd *spd)
{
	struct thd_invocation_frame *inv_frame;
	inv_frame = thd_invstk_top(thd);
	inv_frame->fault.cnt = spd->fault.cnt;

	/* inv_frame->curr_fault.cnt = spd->fault.cnt;*/  // right? done in hijack or inv (brand_next_thread)
	return 0;
}

static inline int
ipc_fault_update(struct invocation_cap *cap_entry, struct spd *dest_spd)
{
	cap_entry->fault.cnt = dest_spd->fault.cnt;
	return 0;
}

static inline int
pop_fault_update(struct thd_invocation_frame *inv_frame, struct thd_invocation_frame *curr_frame)
{
	inv_frame->fault.cnt = curr_frame->spd->fault.cnt;
	return 0;
}

static inline int
switch_thd_fault_update(struct thread *thd)
{
	struct spd *n_spd;
	struct thd_invocation_frame *tif;

	tif    = thd_invstk_top(thd);
	n_spd  = tif->spd;
	
	tif->curr_fault.cnt = n_spd->fault.cnt;
	/* printk("switch_flt_update: thd %d n_spd %d\n", thd_get_id(thd), spd_get_index(n_spd)); */
	return 0;
}

static inline int
interrupt_fault_update(struct thread *next) /* for now, this is the timer thread */
{
	struct spd *n_spd;
	struct thd_invocation_frame *tif;

	tif    = thd_invstk_top(next);
	n_spd  = tif->spd;
	
	tif->curr_fault.cnt = n_spd->fault.cnt;
	return 0;
}

// extern struct invocation_cap invocation_capabilities[MAX_STATIC_CAP];  jiguo

/* cos_syscall_fault_cntl(int spdid, int option, spdid_t d_spdid, unsigned int cap_no) */

static inline unsigned long
fault_cnt_syscall_helper(int spdid, int option, spdid_t d_spdid, unsigned int cap_no)
{
	unsigned long ret = 0;
	struct spd *d_spd, *this_spd, *dest_spd;
	struct invocation_cap *cap_entry;
	unsigned int cap_no_origin;
	
	this_spd = spd_get_by_index(spdid);
	d_spd      = spd_get_by_index(d_spdid);

	if (!this_spd || !d_spd) {
		printk("cos: invalid fault cnt  call for spd %d or spd %d\n",
		       spdid, d_spdid);
		return -1;
	}

	cap_no >>= 20;
	/* printk("cos: cap_no is %d base is %d\n", cap_no, d_spd->cap_base); */

	if (unlikely(cap_no >= MAX_STATIC_CAP)) {
		printk("cos: capability %d greater than max\n",
		       cap_no);
		return -1;
	}

//	cap_entry = &invocation_capabilities[cap_no];   jiguo

	if (unlikely(!cap_entry->owner)) {
		printk("cos: No owner for cap %d.\n", cap_no);
		return -1;
	}

	switch(option) {
	case COS_SPD_FAULT_TRIGGER:
		d_spd->fault.cnt++;
		/* printk("cos: SPD %d Fault.Cnt is incremented by 1\n", spd_get_index(d_spd)); */
		break;
	case COS_CAP_FAULT_UPDATE: /* does not need destination spd, got by cap_no on interface */
		cap_no_origin = cap_no;
		assert(cap_entry->owner == d_spd);

		dest_spd = cap_entry->destination;
		/* printk("dest_spd is %d\n", spd_get_index(dest_spd)); */
		cap_entry->fault.cnt = dest_spd->fault.cnt;

		/* printk("cap_no_origin %d): owner %d dest is %d\n", cap_no_origin, */
		/*        spd_get_index(cap_entry->owner),spd_get_index(cap_entry->destination)); */
		/* printk("cap_entry->fault.cnt %d\n",cap_entry->fault.cnt); */

		/* It seems more reasonable to only update the corresponding cap fault ..... */
		/* not for multiple clients, so still need */

		/* assume capabilities are continuse in invocation_capabilities[], not true... */
		/* + direction */
		while(1) {
			/* printk("++ direction\n"); */
			cap_no_origin++;
			//cap_entry = &invocation_capabilities[cap_no_origin];  jiguo
			if (cap_entry->owner != d_spd) break;
			
			/* printk("ker ++ set cnt+(cap_no_origin %d): owner %d dest is %d\n", cap_no_origin, */
			/*        spd_get_index(cap_entry->owner),spd_get_index(cap_entry->destination)); */

			if (cap_entry->destination != dest_spd) continue;
			cap_entry->fault.cnt = dest_spd->fault.cnt;
			/* printk("cap_entry->fault.cnt %d\n",cap_entry->fault.cnt); */
		}

		cap_no_origin = cap_no;

		/* - direction */
		while(1) {
			/* printk("-- direction\n"); */
			cap_no_origin--;
			//cap_entry = &invocation_capabilities[cap_no_origin];  jiguo
			if (cap_entry->owner != d_spd) break;

			/* printk("ker -- set cnt+(cap_no_origin %d): owner %d dest is %d\n", cap_no_origin, */
			/*        spd_get_index(cap_entry->owner),spd_get_index(cap_entry->destination)); */

			if (cap_entry->destination != dest_spd) continue;
			cap_entry->fault.cnt = dest_spd->fault.cnt;
			/* printk("cap_entry->fault.cnt %d\n",cap_entry->fault.cnt);	 */
		}

		/* printk("cos: CAP (owner %d dest %d) Fault is updated\n", spd_get_index(d_spd), spd_get_index(dest_spd) ); */
		break;
	default:
		ret = -1;
		break;
	}
	
	return ret;
}

#else
//*************************************************
/* disable recovery */
//*************************************************
static inline int
init_spd_fault_cnt(struct spd *spd)
{
	return 0;
}

static inline int
init_cap_fault_cnt(struct invocation_cap *cap)
{
	return 0;
}

static inline int
init_invframe_fault_cnt(struct thd_invocation_frame *inv_frame)
{
	return 0;
}

static inline int
inv_frame_fault_cnt_update(struct thread *thd, struct spd *spd)
{
	return 0;
}

/**************  detect ************/
static inline int
ipc_fault_detect(struct invocation_cap *cap_entry, struct spd *dest_spd)
{
	return 0;
}

static inline int
pop_fault_detect(struct thd_invocation_frame *inv_frame, struct thd_invocation_frame *curr_frame)
{
	return 0;
}

static inline int
switch_thd_fault_detect(struct thread *next)
{
	return 0;
}

static inline int
interrupt_fault_detect(struct thread *next)
{
	return 0;
}

/**************  update ************/
static inline int
ipc_fault_update(struct invocation_cap *cap_entry, struct spd *dest_spd)
{
	return 0;
}

static inline int
pop_fault_update(struct thd_invocation_frame *inv_frame, struct thd_invocation_frame *curr_frame)
{
	return 0;
}

static inline int
switch_thd_fault_update(struct thread *next)
{
	return 0;
}

static inline int
interrupt_fault_update(struct thread *next)
{
	return 0;
}


static inline unsigned long
fault_cnt_syscall_helper(int spdid, int option, spdid_t d_spdid, unsigned int cap_no)
{
	return 0;
}
#endif

#endif  //RECOVERY_H
