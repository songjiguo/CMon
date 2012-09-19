/* For printing: */

#include <stdio.h>
#include <string.h>

static int 
prints(char *s)
{
	int len = strlen(s);
	cos_print(s, len);
	return len;
}

static int __attribute__((format(printf,1,2))) 
printc(char *fmt, ...)
{
	char s[128];
	va_list arg_ptr;
	int ret, len = 128;

	va_start(arg_ptr, fmt);
	ret = vsnprintf(s, len, fmt, arg_ptr);
	va_end(arg_ptr);
	cos_print(s, ret);

	return ret;
}
/* On assert, immediately switch to the "exit" thread */
#define assert(node) do { if (unlikely(!(node))) { debug_print("assert error in @ "); cos_switch_thread(alpha, 0);} } while(0)


#ifdef BOOT_DEPS_H
#error "boot_deps.h should not be included more than once, or in anything other than boot."
#endif
#define BOOT_DEPS_H

#include <cos_component.h>
//#include <print.h>
#include <res_spec.h>

/* 
 * alpha:        the initial thread context for the system
 * init_thd:     the thread used for the initialization of the rest 
 *               of the system
 * recovery_thd: the thread to perform initialization/recovery
 * prev_thd:     the thread requesting the initialization
 * recover_spd:  the spd that will require rebooting
 */
static int          alpha, init_thd, recovery_thd;	
static volatile int prev_thd, recover_spd;

static volatile vaddr_t recovery_arg;
static volatile int operation;

enum { /* hard-coded initialization schedule */
	LLBOOT_SCHED = 2,
	LLBOOT_MM    = 3,
};

struct comp_boot_info {
	int symbols_initialized, initialized, memory_granted;
};
#define NCOMPS 6 	/* comp0, us, and the four other components */
static struct comp_boot_info comp_boot_nfo[NCOMPS];

static spdid_t init_schedule[]   = {LLBOOT_MM, LLBOOT_SCHED, 0};
static int     init_mem_access[] = {1, 0, 0};
static int     sched_offset      = 0, nmmgrs = 0;
static int     frame_frontier    = 0; /* which physical frames have we used? */

typedef void (*crt_thd_fn_t)(void);

/* 
 * Abstraction layer around 1) synchronization, 2) scheduling and
 * thread creation, and 3) memory operations.  
 */

#include "../../sched/cos_sched_sync.h"
/* synchronization... */
#define LOCK()   if (cos_sched_lock_take())    BUG();
#define UNLOCK() if (cos_sched_lock_release()) BUG();

/* scheduling/thread operations... */

/* We'll do all of our own initialization... */
static int
sched_create_thread_default(spdid_t spdid, u32_t sched_param_0, 
			    u32_t sched_param_1, unsigned int desired_thd)
{ return 0; }

static void
llboot_ret_thd(void) { return; }

/* 
 * When a created thread finishes, here we decide what to do with it.
 * If the system's initialization thread finishes, we know to reboot.
 * Otherwise, we know that recovery is complete, or should be done.
 */
static void
llboot_thd_done(void)
{
	int tid = cos_get_thd_id();

	assert(alpha);
	/* 
	 * When the initial thread is done, then all we have to do is
	 * switch back to alpha who should reboot the system.
	 */
	if (tid == init_thd) {
		spdid_t s = init_schedule[sched_offset];

		/* Is it done, or do we need to initialize another component? */
		if (s) {
			/* If we have a memory manger, give it a
			 * proportional amount of memory WRT to the
			 * other memory managers. */
			if (init_mem_access[sched_offset]) {
				int max_pfn, proportion;

				max_pfn = cos_pfn_cntl(COS_PFN_MAX_MEM, 0, 0, 0);
				proportion = (max_pfn - frame_frontier)/nmmgrs;
				cos_pfn_cntl(COS_PFN_GRANT, s, frame_frontier, proportion);
				comp_boot_nfo[s].memory_granted = 1;
			}
			sched_offset++;
			comp_boot_nfo[s].initialized = 1;
			cos_upcall(s); /* initialize the component! */
			BUG();
		}
		/* Done initializing; reboot!  If we are here, then
		 * all of the threads have terminated, thus there is
		 * no more execution left to do.  Technically, the
		 * other components should have called
		 * sched_exit... */
		while (1) cos_switch_thread(alpha, 0);
		BUG();
	}
	
	while (1) {
		int     pthd  = prev_thd;
		spdid_t rspd  = recover_spd;
		int     op    = operation;
		vaddr_t arg  =  recovery_arg;
				
		assert(tid == recovery_thd);
		if (rspd) {             /* need to recover a component */
			assert(pthd);
			recover_spd = 0;
			cos_upcall_args(op, rspd, arg); /* This will escape from the loop */
			assert(0);
		} else {		/* ...done reinitializing...resume */
			assert(pthd && pthd != tid);
			prev_thd = 0;   /* FIXME: atomic action required... */
			cos_switch_thread(pthd, 0);
		}
	}
}

/* can only be called from mmgr/scheduler */
int
recovery_upcall(spdid_t spdid, int op, spdid_t dest, vaddr_t arg)
{
	printc("LL: llbooter upcall to spd %d, arg %x thd %d\n", dest, (unsigned int)arg, cos_get_thd_id());

	prev_thd     = cos_get_thd_id();	/* this ensure that prev_thd is always the highest prio thread */
	recover_spd  = dest;
	operation    = op;
	recovery_arg = arg;
	
	while (prev_thd == cos_get_thd_id()) cos_switch_thread(recovery_thd, 0);

	return 0;
}

void 
failure_notif_fail(spdid_t caller, spdid_t failed);


void sched_exit(void);

static int first = 0;
int 
fault_page_fault_handler(spdid_t spdid, void *fault_addr, int flags, void *ip)
{
	unsigned long r_ip; 	/* the ip to return to */

	int tid = cos_get_thd_id();

	int reboot;
	reboot = (spdid == 2) ? 1:0;

	first++;
	if(first == 5) {
		printc("has failed %d times\n",first);
		sched_exit();
	}
	/* printc("LL: recovery_thd %d, alpha %d, init_thd %d\n", recovery_thd, alpha, init_thd); */
	/* printc("LL: <<0>> thd %d : failed spd %d (this spd %d)\n", cos_get_thd_id(), spdid, cos_spd_id()); */

	printc("LL: <<0>> thd %d failed in spd %d\n", cos_get_thd_id(), spdid);

	failure_notif_fail(cos_spd_id(), spdid);
	printc("LL: <<1>>\n");
	/* no reason to save register contents... */
	/* unsigned long long start, end; */
	/* rdtscll(start); */

	if(!cos_thd_cntl(COS_THD_INV_FRAME_REM, tid, 1, 0)) {
		/* Manipulate the return address of the component that called
		 * the faulting component... */
		assert(r_ip = cos_thd_cntl(COS_THD_INVFRM_IP, tid, 1, 0));
		/* ...and set it to its value -8, which is the fault handler
		 * of the stub. */
		assert(!cos_thd_cntl(COS_THD_INVFRM_SET_IP, tid, 1, r_ip-8));

		assert(!cos_fault_cntl(COS_SPD_FAULT_TRIGGER, spdid, 0));

		printc("Try to recover the spd\n");
		if (reboot) recovery_upcall(cos_spd_id(), COS_UPCALL_REBOOT, spdid, 0);
		else recovery_upcall(cos_spd_id(), COS_UPCALL_BOOTSTRAP, spdid, 0);
		/* after the recovery thread is done, it should switch back to us. */
		/* rdtscll(end); */
		/* printc("COST (rest of fault_handler) : %llu\n", end - start); */
		return 0;
	}
	
	printc("LL: <<2>>\n");
	/* 
	 * The thread was created in the failed component...just use
	 * it to restart the component!  This might even be the
	 * initial thread.
	 */
	if (reboot) cos_upcall_args(COS_UPCALL_FAILURE_NOTIF, spdid, 0);
	else cos_upcall(spdid); 	/* FIXME: give back stack... */
	BUG();

	return 0;
}

#ifdef NOTIF_TEST
int
fault_flt_notif_handler(spdid_t spdid, void *fault_addr, int flags, void *ip)
{
	printc("parameters: spdid %d fault_addr %p flags %d ip %p\n", spdid, fault_addr, flags, ip);
	unsigned long r_ip;

	int tid = cos_get_thd_id();
	printc("<< LL fault notification handler >> :: thd %d saw that spd %d has failed before\n", cos_get_thd_id(), spdid);

	if(!cos_thd_cntl(COS_THD_INV_FRAME_REM, tid, 1, 0)) {
		assert(r_ip = cos_thd_cntl(COS_THD_INVFRM_IP, tid, 1, 0));
		assert(!cos_thd_cntl(COS_THD_INVFRM_SET_IP, tid, 1, r_ip-8));
		return 0;
	}

	if (tid == 7) cos_upcall_args(COS_UPCALL_BRAND_EXEC, spdid, 0);	/* timer thread */
	else cos_upcall(flags);	/* upcall to ths dest spd, for other threads */

	assert(0);
}

#endif

/* memory operations... */

static vaddr_t init_hp = 0; 		/* initial heap pointer */
/* 
 * Assumptions about the memory management functions: 
 * - we only get single-page-increasing virtual addresses to map into.
 * - we never deallocate memory.
 * - we allocate memory contiguously
 * Many of these assumptions are ensured by the following code.
 * cos_get_vas_page should allocate vas contiguously, and incrementing
 * by a page, and the free function is made empty.
 */

/* 
 * Virtual address to frame calculation...assume the first address
 * passed in is the start of the heap, and they only increase by a
 * page from there.
 */
static inline int
__vpage2frame(vaddr_t addr) { return (addr - init_hp) / PAGE_SIZE; }

static vaddr_t
__mman_get_page(spdid_t spd, vaddr_t addr, int flags)
{
	if (cos_mmap_cntl(COS_MMAP_GRANT, 0, cos_spd_id(), addr, frame_frontier++)) BUG();
	if (!init_hp) init_hp = addr;
	return addr;
}

static vaddr_t
__mman_alias_page(spdid_t s_spd, vaddr_t s_addr, spdid_t d_spd, vaddr_t d_addr)
{
	int fp;

	assert(init_hp);
	fp = __vpage2frame(s_addr);
	assert(fp >= 0);
	if (cos_mmap_cntl(COS_MMAP_GRANT, 0, d_spd, d_addr, fp)) BUG();
	return d_addr;
}

static int boot_spd_set_symbs(struct cobj_header *h, spdid_t spdid, struct cos_component_information *ci);
static void
comp_info_record(struct cobj_header *h, spdid_t spdid, struct cos_component_information *ci) 
{ 
	if (!comp_boot_nfo[spdid].symbols_initialized) {
		comp_boot_nfo[spdid].symbols_initialized = 1;
		boot_spd_set_symbs(h, spdid, ci);
	}
}

static void
boot_deps_init(void)
{
	int i;
	
	alpha        = cos_get_thd_id();
	recovery_thd = cos_create_thread((int)llboot_ret_thd, (int)0, 0);
	assert(recovery_thd >= 0);
	init_thd     = cos_create_thread((int)llboot_ret_thd, 0, 0);
	printc("Low-level booter created threads:\n\t"
	       "%d: alpha\n\t%d: recov\n\t%d: init\n",
	       alpha, recovery_thd, init_thd);
	assert(init_thd >= 0);

	/* How many memory managers are there? */
	for (i = 0 ; init_schedule[i] ; i++) nmmgrs += init_mem_access[i];
	assert(nmmgrs > 0);
}

static void
boot_deps_run(void)
{
	assert(init_thd);
	cos_switch_thread(init_thd, 0);
}

void 
cos_upcall_fn(upcall_type_t t, void *arg1, void *arg2, void *arg3)
{
	switch (t) {
	case COS_UPCALL_CREATE:
		cos_argreg_init();
		((crt_thd_fn_t)arg1)();
		break;
	case COS_UPCALL_DESTROY:
		llboot_thd_done();
		break;
	case COS_UPCALL_UNHANDLED_FAULT:
		printc("Fault detected by the llboot component in thread %d: "
		       "Major system error.\n", cos_get_thd_id());
	default:
		while (1) ;
		return;
	}

	return;
}

#include <sched_hier.h>

void cos_init(void);
int  sched_init(int reboot)   { printc("llboot sched_init\n"); cos_init(); return 0; }
int  sched_isroot(void) { return 1; }
void 
sched_exit(void)
{
	while (1) cos_switch_thread(alpha, 0);	
}

int 
sched_child_get_evt(spdid_t spdid, struct sched_child_evt *e, int idle, unsigned long wake_diff) 
{ BUG(); return 0; }

int 
sched_child_cntl_thd(spdid_t spdid) 
{
	if (unlikely(cos_sched_introspect(COS_SCHED_HAS_PARENT, spdid, 0))) return 0;
	if (cos_sched_cntl(COS_SCHED_PROMOTE_CHLD, 0, spdid)) BUG();
	if (cos_sched_cntl(COS_SCHED_GRANT_SCHED, cos_get_thd_id(), spdid)) BUG();
	return 0;
}

int 
sched_child_thd_crt(spdid_t spdid, spdid_t dest_spd) { BUG(); return 0; }