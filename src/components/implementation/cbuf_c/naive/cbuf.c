/**
 * Copyright 2010 by The George Washington University.  All rights reserved.
 *
 * Redistribution of this file is permitted under the GNU General
 * Public License v2.
 *
 * Author: Gabriel Parmer, gparmer@gwu.edu, 2010
 */

#include <cos_component.h>
#include <sched.h>
#include <cos_synchronization.h>
#include <print.h>
#include <cos_alloc.h>
#include <cos_map.h>
#include <cos_list.h>
#include <cos_debug.h>

#include <cinfo.h>

#include <mem_mgr_large.h>
#include <valloc.h>
#include <mem_pool.h>

#include <tmem.h>
#include <cbuf_c.h>


//#define PRINCIPAL_CHECKS

#define DEFAULT_TARGET_ALLOC MAX_NUM_MEM 

COS_MAP_CREATE_STATIC(cb_ids);

#define CBUF_OBJ_SZ_SHIFT 6
#define CB_IDX(id, cbr) (id - cbr->start_id - 1)

struct cos_cbuf_item *alloc_item_data_struct(void *l_addr) 
{
	struct cos_cbuf_item *cci;
	cci = malloc(sizeof(struct cos_cbuf_item));
	if (!cci) BUG();

	INIT_LIST(cci, next, prev);
        
	cci->desc.addr = l_addr;

	cci->desc.cbid = 0;
	cci->desc.obj_sz = 0;
	cci->desc.principal = 0;

	return cci;
}

void free_item_data_struct(struct cos_cbuf_item *tmi) 
{
	free(tmi);
}

struct cos_cbuf_item *free_mem_in_local_cache(struct spd_tmem_info *sti)
{
	spdid_t s_spdid;
	struct cos_cbuf_item *cci = NULL, *list;

	assert(sti);
	s_spdid = sti->spdid;

	DOUT("\n Check if in local cache!!!");
	list = &spd_tmem_info_list[s_spdid].tmem_list;
	/* Go through the allocated cbufs, and see if any are not in use... */
	for (cci = FIRST_LIST(list, next, prev) ; 
	     cci != list; 
	     cci = FIRST_LIST(cci, next, prev)) {
		union cbuf_meta cm;
		cm.c_0.v = cci->entry->c_0.v;
		if (!CBUF_IN_USE(cm.c.flags)) goto done;
	}

	if (cci == list) goto err;
done:
	/* DOUT("\n Found one!!\n\n"); */
	return cci;
err:
	/* DOUT("\n can not found one!!\n"); */
	cci = NULL;	
	return cci;
}

//  all cbufs that created for this component
void mgr_map_client_mem(struct cos_cbuf_item *cci, struct spd_tmem_info *sti)
{
	char *l_addr, *d_addr;
	spdid_t d_spdid;
//	struct cb_desc *d;

	assert(sti && cci);
	assert(EMPTY_LIST(cci, next, prev));

	d_spdid = sti->spdid;

	/* TODO: multiple pages cbuf! */
	d_addr = valloc_alloc(cos_spd_id(), sti->spdid, 1);
	l_addr = cci->desc.addr;  //initialized in cos_init()

	assert(d_addr && l_addr); 

	/* ...map it into the requesting component */
	if (unlikely(!mman_alias_page(cos_spd_id(), (vaddr_t)l_addr, d_spdid, (vaddr_t)d_addr))) 
		goto err;
	/* DOUT("<<<MAPPED>>> mgr addr %p client addr %p\n ",l_addr, d_addr); */
	cci->desc.owner.addr = (vaddr_t)d_addr;
	cci->parent_spdid = d_spdid;
	assert(cci->desc.cbid == 0);
	// add the cbuf to shared vect here? now we do it in the client.
	// and l_addr and d_addr has been assinged
done:
	return;
err:
	DOUT("Cbuf mgr: Cannot alias page to client!\n");
	mman_release_page(cos_spd_id(), (vaddr_t)l_addr, 0);
	/* valloc_free(cos_spd_id(), cos_spd_id(), l_addr, 1); */
	valloc_free(cos_spd_id(), d_spdid, (void *)d_addr, 1);
	goto done;
}

int __cbuf_c_delete(struct spd_tmem_info *sti, int cbid, struct cb_desc *d);

static void
mgr_remove_client_mem(struct spd_tmem_info *sti, struct cos_cbuf_item *cci)
{
	__cbuf_c_delete(sti, cci->desc.cbid, &cci->desc);
	DOUT("already delete....cbid %d\n", cci->desc.cbid);

	cos_map_del(&cb_ids, cci->desc.cbid);
	DOUT("fly..........cbid is %d\n", cci->desc.cbid);

	cci->desc.cbid = 0;
	cci->parent_spdid = 0;

	// Clear our memory to prevent leakage
	memset(cci->desc.addr, 0, PAGE_SIZE);
	/* printc("Removing from local list\n"); */

	REM_LIST(cci, next, prev);

	/* TODO: move all of this into the tmem generic code just like the ++s */
	sti->num_allocated--;
	if (sti->num_allocated == 0) empty_comps++;

	if (sti->num_allocated >= sti->num_desired) over_quota_total--;
	assert(sti->num_allocated == tmem_num_alloc_tmems(sti->spdid));
}

struct cos_cbuf_item *mgr_get_client_mem(struct spd_tmem_info *sti)
{
	spdid_t s_spdid;
	/* struct cb_desc *d; */
	struct cos_cbuf_item *cci = NULL, *list;
	assert(sti);
	s_spdid = sti->spdid;

	list = &spd_tmem_info_list[s_spdid].tmem_list;

	for (cci = FIRST_LIST(list, next, prev) ; 
	     cci != list ; 
	     cci = FIRST_LIST(cci, next, prev)) {
		union cbuf_meta cm;
		cm.c_0.v = cci->entry->c_0.v;
		if (!CBUF_IN_USE(cm.c.flags)) break;
	}

	if (cci == list) goto err;
	assert(&cci->desc == cos_map_lookup(&cb_ids, cci->desc.cbid));
	/* struct cb_mapping *m; */
	/* m = FIRST_LIST(&cci->desc.owner, next, prev); */
	
	mgr_remove_client_mem(sti, cci);

	DOUT("spd: %d Leaving get cli mem:: num_allocated %d  num_desired %d\n",s_spdid, sti->num_allocated, sti->num_desired);

done:
	return cci;
err: 
	cci = NULL;
	goto done;
}

int
resolve_dependency(struct spd_tmem_info *sti, int skip_cbuf)
{
	struct cos_cbuf_item *cci;
	/* union cbuf_meta cm; */

	int ret = -1;

	/* DOUT("skip_cbuf is %d\n",skip_cbuf); */

	for(cci = FIRST_LIST(&sti->tmem_list, next, prev);
	    cci != &sti->tmem_list && skip_cbuf > 0; 
	    cci = FIRST_LIST(cci, next, prev), skip_cbuf--) ;

	if (cci == &sti->tmem_list) goto done;

	union cbuf_meta cm;
	cm.c_0.v = cci->entry->c_0.v;			

	ret = (u32_t)cci->entry->c_0.th_id;

	if (!CBUF_IN_USE(cm.c.flags)) goto cache;

	/* DOUT("cm.c_0.v is %p \n", cm.c_0.v); */
	// Jiguo: A thread could ask for multiple cbuf items, so it 
	// could find to be dependent on itself
	/* DOUT("ret :: %d current thd : %d \n", ret, cos_get_thd_id()); */
	if (ret == cos_get_thd_id()){
		DOUT("Try to depend on itself ....\n");
		goto self;
	}

done:
	return ret;
cache:
	ret = -2;
	goto done;
self:
	ret = 0;
	goto done;
}

void mgr_clear_touched_flag(struct spd_tmem_info *sti)
{
	struct cos_cbuf_item *cci;
	union cbuf_meta *cm = NULL;

	for (cci = FIRST_LIST(&sti->tmem_list, next, prev) ; 
	     cci != &sti->tmem_list ; 
	     cci = FIRST_LIST(cci, next, prev)) {
		cm = cci->entry;
		if (!CBUF_IN_USE(cm->c.flags)) {
			cm->c.flags &= ~CBUFM_TOUCHED;
		} else {
			assert(cm->c.flags & CBUFM_TOUCHED);
		}
	}
	return;
}

static inline int
__spd_cbvect_add_range(struct spd_tmem_info *sti, long cbuf_id, vaddr_t page)
{
	struct spd_cbvect_range *cbr;

	cbr = malloc(sizeof(struct spd_cbvect_range));
	if (!cbr) return -1;

	cbr->start_id = (cbuf_id - 1) & ~CVECT_MASK;
	cbr->end_id = cbr->start_id + CVECT_BASE - 1;
	cbr->meta = (union cbuf_meta*)page;

	/* DOUT("spd %d  sti %p cbr->meta %p\n",sti->spdid,sti, cbr->meta); */
	ADD_LIST(&sti->ci, cbr, next, prev);

	/* DOUT("range is added here:: startid %ld endid %ld\n", cbr->start_id, cbr->end_id); */

	return 0;
}

static inline union cbuf_meta *
__spd_cbvect_lookup_range(struct spd_tmem_info *sti, long cbuf_id)
{
	struct spd_cbvect_range *cbr;

	for (cbr = FIRST_LIST(&sti->ci, next, prev) ; 
	     cbr != &sti->ci ; 
	     cbr = FIRST_LIST(cbr, next, prev)) {
		if (cbuf_id >= cbr->start_id && cbuf_id <= cbr->end_id) {
			return &cbr->meta[CB_IDX(cbuf_id, cbr)];
		}
	}
	return NULL;
}


static inline void
__spd_cbvect_clean_val(struct spd_tmem_info *sti, long cbuf_id)
{
	struct spd_cbvect_range *cbr;

	for (cbr = FIRST_LIST(&sti->ci, next, prev) ; 
	     cbr != &sti->ci ; 
	     cbr = FIRST_LIST(cbr, next, prev)) {
		if (cbuf_id >= cbr->start_id && cbuf_id <= cbr->end_id) {
			cbr->meta[CB_IDX(cbuf_id, cbr)].c_0.v = (u32_t)COS_VECT_INIT_VAL;
			cbr->meta[CB_IDX(cbuf_id, cbr)].c_0.th_id = (u32_t)COS_VECT_INIT_VAL;
			break;
		}
	}
	return;
}


static inline vaddr_t
__spd_cbvect_retrieve_page(struct spd_tmem_info *sti, long cbuf_id)
{
	struct spd_cbvect_range *cbr;

	for (cbr = FIRST_LIST(&sti->ci, next, prev) ; 
	     cbr != &sti->ci ; 
	     cbr = FIRST_LIST(cbr, next, prev)) {
		if (cbuf_id >= cbr->start_id && cbuf_id <= cbr->end_id) {
			return (vaddr_t)cbr->meta;
		}
	}
	return 0;
}

vaddr_t
cbuf_c_register(spdid_t spdid, long cbid)
{
	struct spd_tmem_info *sti;
	vaddr_t p, mgr_addr;

	/* DOUT("\nREGISTERED!!!\n"); */
	sti = get_spd_info(spdid);
	
	mgr_addr = (vaddr_t)alloc_page();
	p = (vaddr_t)valloc_alloc(cos_spd_id(), spdid, 1);
	if (p !=
	    (mman_alias_page(cos_spd_id(), mgr_addr, spdid, p))) {
		DOUT("mapped faied p is %p\n",(void *)p);
		valloc_free(cos_spd_id(), spdid, (void *)p, 1);
		return -1;
	}
	sti->managed = 1;
	/* __spd_cbvect_add_range(sti, cbid, (struct cbuf_vect_intern_struct *)mgr_addr); */
	__spd_cbvect_add_range(sti, cbid, mgr_addr);

	return p;
}
 
int
cbuf_c_create(spdid_t spdid, int size, long cbid)
{
	int ret = -1;
	void *v;
	struct spd_tmem_info *sti;
	struct cos_cbuf_item *cbuf_item;
	struct cb_desc *d;

	union cbuf_meta *mc = NULL;

	/* DOUT("thd: %d spd: %d cbuf_c_create is called here!!\n", cos_get_thd_id(), spdid); */
	/* DOUT("passed cbid is %ld\n",cbid); */
	TAKE();

	sti = get_spd_info(spdid);
	
	/* Make sure we have access to the component shared page */
	assert(SPD_IS_MANAGED(sti));
	assert(cbid >= 0);

	/* For cbuf only
	 * find an unused cbuf_id
	 * looks through to find id
	 * if does not find, allocate a page for the meta data
	 */
	if (cbid) {
		 // vector should already exist
		v = cos_map_lookup(&cb_ids, cbid);
		if (unlikely((spdid_t)(int)v != spdid)) goto err;
 	} else {
		cbid = cos_map_add(&cb_ids, (void *)(unsigned long)spdid);
		if ((mc = __spd_cbvect_lookup_range(sti, (cbid))) == NULL){
			RELEASE();
			return cbid*-1;	
		} 
	}
	cos_map_del(&cb_ids, cbid);
	cbuf_item = tmem_grant(sti);
	assert(cbuf_item);

	d             = &cbuf_item->desc;
	d->principal  = cos_get_thd_id();
	d->obj_sz     = PAGE_SIZE;
	d->owner.spd  = sti->spdid;
	d->owner.cbd  = d;

	/* initialize cfi for torrent FT now*/
	d->cfi.len    = 0;
	d->cfi.offset = 0;
	d->cfi.fid    = -1;  	/* unique map id starts from 0 */

	/* Jiguo:
	  This can be two different cases:
	  1. A local cached one is returned with a cbid
	  2. A cbuf item is obtained from the global free list without cbid
	 */
	DOUT("d->cbid is %d\n",d->cbid);
	if (d->cbid == 0) {
		INIT_LIST(&d->owner, next, prev);  // only created when first time
		cbid = cos_map_add(&cb_ids, d);    // we use a new cbuf
		DOUT("new cbid is %ld\n",cbid);
	} else {
		cbid = cbuf_item->desc.cbid;       // use a local cached one
		DOUT("cached cbid is %ld\n",cbid);
	}

	DOUT("cbuf_create:::new cbid is %ld\n",cbid);
	ret = d->cbid = cbid;

	mc = __spd_cbvect_lookup_range(sti, cbid);
	assert(mc);
	cbuf_item->entry = mc;

	mc->c.ptr     = d->owner.addr >> PAGE_ORDER;

	/* Crap solution to get rid of a compiler warning... */
	mc->c.obj_sz  = (u16_t) 0x3F & 
		(((u16_t)PAGE_SIZE) >> (u16_t)CBUF_OBJ_SZ_SHIFT);
	mc->c_0.th_id = cos_get_thd_id();
	mc->c.flags  |= CBUFM_IN_USE | CBUFM_TOUCHED;
	mc->c.flags  |= CBUFM_OWNER;
done:
	RELEASE();
	return ret;
err:
	ret = -1;
	goto done;
}


int __cbuf_c_delete(struct spd_tmem_info *sti, int cbid, struct cb_desc *d)
{
	struct cb_mapping *m;
	struct spd_tmem_info *map_sti;
	DOUT("__cbuf_c_delete....cbid %d\n", cbid);

	__spd_cbvect_clean_val(sti, cbid);
	mman_revoke_page(cos_spd_id(), (vaddr_t)d->addr, 0);  // remove all mapped children

	m = FIRST_LIST(&d->owner, next, prev);
	
	while (m != &d->owner) {
		struct cb_mapping *n;

		/* remove from the vector in all mapped spds as well! */
		map_sti = get_spd_info(m->spd);
		DOUT("clear cbid %d in %d\n",cbid, map_sti->spdid);
		__spd_cbvect_clean_val(map_sti, cbid);

		valloc_free(cos_spd_id(), m->spd, (void *)(m->addr), 1);
		n = FIRST_LIST(m, next, prev);
		REM_LIST(m, next, prev);
		free(m);
		m = n;
	}

	valloc_free(cos_spd_id(), sti->spdid, (void *)(d->owner.addr), 1);
	sti->ci.spd_cinfo_page->cos_tmem_available[COMP_INFO_TMEM_CBUF]--;

	DOUT("unmapped is done\n");
	return 0;
}

/* 
 * FIXME: 1) reference counting so that components can maintain the
 * buffer if they please, 2) asynchronous (shmmem) notification of cb
 * deallocation.
 */
int
cbuf_c_delete(spdid_t spdid, int cbid)
{
	struct spd_tmem_info *sti;
	int ret = 0;  /* return value not used */

	TAKE();

	sti = get_spd_info(spdid);
	assert(sti);

	return_tmem(sti);

	RELEASE();
	return ret;
}

void *
cbuf_c_retrieve(spdid_t spdid, int cbid, int len)
{
	void *ret = NULL;
	char *l_addr, *d_addr;

	struct cb_desc *d;
	struct cb_mapping *m;

	TAKE();

	d = cos_map_lookup(&cb_ids, cbid);
	/* sanity and access checks */
	if (!d || d->obj_sz < len) goto done;
#ifdef PRINCIPAL_CHECKS
	if (d->principal != cos_get_thd_id()) goto done;
#endif
	/* DOUT("info: thd_id %d obj_size %d addr %p\n", d->principal, d->obj_sz, d->addr); */
	m = malloc(sizeof(struct cb_mapping));
	if (!m) goto done;

	/* u64_t start,end; */
	/* rdtscll(start); */

	INIT_LIST(m, next, prev);

	d_addr = valloc_alloc(cos_spd_id(), spdid, 1);
	l_addr = d->addr;  //cbuf_item addr, initialized in cos_init()
	/* l_addr = d->owner.addr;  // mapped from owner */

	assert(d_addr && l_addr);

	/* rdtscll(end); */
	/* printc("cost of valloc: %lu\n", end-start); */
	/* rdtscll(start); */

	/* if (!mman_alias_page(cos_spd_id(), (vaddr_t)d->addr, spdid, (vaddr_t)page)) goto err; */
	if (unlikely(!mman_alias_page(cos_spd_id(), (vaddr_t)l_addr, spdid, (vaddr_t)d_addr))) {
		printc("No alias!\n");
		goto err;
	}
	/* DOUT("<<<MAPPED>>> mgr addr %p client addr %p\n ",l_addr, d_addr); */

	/* rdtscll(end); */
	/* printc("cost of mman_alias_page: %lu\n", end-start); */

	m->cbd  = d;
	m->spd  = spdid;
	m->addr = (vaddr_t)d_addr;

	//struct cb_mapping *m;
	ADD_LIST(&d->owner, m, next, prev);
	ret = (void *)d_addr;
done:
	RELEASE();
	return ret;
err:
	valloc_free(cos_spd_id(), spdid, d_addr, 1);
	free(m);
	goto done;
}


struct cb_file_info cfi_list;

/* FIXME: the writing mode is not defined yet. Now if close a file
 * ,and reopen it, the old contents is overwritten. In the future, for
 * ramFS, other mode should be supported, like append writing.... */
/* 
   len   :  actual written size
   offset:  offset before writing  
   fid   :  unique file id obtained from unique map
   cbid  :  the one that carries the data
 */
int 
cbuf_c_record(int cbid, int len, int offset, int fid)
{
	int ret = 0;
	struct cb_desc *d, *f_d;
	struct cb_file_info *cfi;
	int id;

	/* printc("cbuf_c_record:: cbid %d len %d offset %d fid %d\n", cbid, len, offset, fid); */

	assert(cbid >= 0 && len > 0);

	TAKE();
	d = cos_map_lookup(&cb_ids, cbid); /* ramFS should have claimed the ownership  */
	if (!d) goto err;
	
	assert(d->owner.spd == 15); /* test purpose only, should pass spdid into here */
	/* assert(d->cfi.fid == -1);   /\* initialized to -1 *\/ */
	id  = d->cfi.fid;

	d->cfi.fid    = fid;
	d->cfi.len    = len;
	d->cfi.offset = offset;
	d->cfi.cbd    = d;

	/* printc("when record:  address %p\n", d->owner.addr); */

	INIT_LIST(&d->cfi, next, prev);
	INIT_LIST(&d->cfi, down, up); 	/* the writes on the same torrent */
	
	for (cfi = FIRST_LIST(&cfi_list, next, prev);
	     cfi != &cfi_list;
	     cfi = FIRST_LIST(cfi, next, prev)) {
		if (cfi->fid == fid) {
			if (offset == 0) { /* tsplit is called here, so discard any previous data */
				cfi->fid    = fid;
				cfi->len    = len;
				cfi->offset = offset;
				cfi->cbd    = d;
				/* should free all previous nodes?? */
				INIT_LIST(cfi, down, up);
			} else {
				ADD_LIST(cfi, &d->cfi, down, up);
			}
			goto done;
		}
	}

	
	ADD_LIST(&cfi_list, &d->cfi, next, prev);

done:
	/* for (cfi = FIRST_LIST(&cfi_list, next, prev); */
	/*      cfi != &cfi_list; */
	/*      cfi = FIRST_LIST(cfi, next, prev)) { */
	/* 	printc("fid %d\n",cfi->fid); */
	/* 	printc("len %d\n",cfi->len); */
	/* 	printc("offset %d\n",cfi->offset); */
	/* 	printc("cbid %d\n",cfi->cbd->cbid); */
	/* 	printc("owner spd  %d\n",cfi->cbd->owner.spd); */
	/* } */
	

	RELEASE();
	return ret;
err:
	ret = -1;
	goto done;
}


static int return_item(int id, int nth, int flag)
{
	struct cb_file_info *cfi, *cfi_d;
	int ret = 0;
	for (cfi = LAST_LIST(&cfi_list, next, prev);
	     cfi != &cfi_list;
	     cfi = LAST_LIST(cfi, next, prev)) {
		if (cfi->fid == id) {
			nth--;
			if (!nth) {
				if (flag == CBUF_INTRO_CBID) {
					ret = cfi->cbd->cbid;
					/* printc("owner spd %d\n", cfi->cbd->owner.spd); */
					/* printc("its address %p\n", cfi->cbd->owner.addr); */
				}
				if (flag == CBUF_INTRO_SIZE) ret = cfi->len;
				if (flag == CBUF_INTRO_OFFSET) ret = cfi->offset;
				return ret;
			} else {
				for (cfi_d = FIRST_LIST(cfi, down, up) ;
				     cfi_d != cfi;
				     cfi_d = FIRST_LIST(cfi_d, down, up)) {
					nth--;
					if (!nth) {
						if (flag == CBUF_INTRO_CBID) ret = cfi_d->cbd->cbid;
						if (flag == CBUF_INTRO_SIZE) ret = cfi_d->len;
						if (flag == CBUF_INTRO_OFFSET) ret = cfi_d->offset;
						return ret;
					}
				}
			}
		}
	}
	return ret;
}


/* id can be either cbid, or unique file id, or something else...., depends on flag */
unsigned int
cbuf_c_introspect(spdid_t spdid, int id, int nth, int flag)
{
	unsigned int ret = 0;
	struct spd_tmem_info *sti;
	spdid_t s_spdid;
	struct cos_cbuf_item *cci = NULL, *list;
	struct cb_desc *d;
	struct cb_mapping *m;

	struct cb_file_info *cfi, *cfi_d;
	
	int counter = 0;
	void *addr = NULL;

	TAKE();

	sti = get_spd_info(spdid);
	assert(sti);
	s_spdid = sti->spdid;
	list = &spd_tmem_info_list[s_spdid].tmem_list;

	switch (flag) {
	case CBUF_INTRO_CBID:   /* introspect from the unique file id here*/
	{
		ret = return_item(id, nth, flag);
		break;
	}
	case CBUF_INTRO_SIZE:
	{
		ret = return_item(id, nth, flag);
		break;
	}
	case CBUF_INTRO_OFFSET:
	{
		ret = return_item(id, nth, flag);
		break;
	}
	case CBUF_INTRO_TOT:   /* find total numbers of cbufs that is being hold by fid */
	{
		for (cfi = FIRST_LIST(&cfi_list, next, prev) ;
		     cfi != &cfi_list;
		     cfi = FIRST_LIST(cfi, next, prev)) {
			if (cfi->fid == id) {
				counter++;
				for (cfi_d = FIRST_LIST(cfi, down, up) ;
				     cfi_d != cfi;
				     cfi_d = FIRST_LIST(cfi_d, down, up)) {
					counter++;
				}
				ret = counter;
				goto done;
			}
		}
		break;
	}
	case CBUF_INTRO_TOT_TOR:   /* find total numbers of tors */
	{
		for (cfi = FIRST_LIST(&cfi_list, next, prev) ;
		     cfi != &cfi_list;
		     cfi = FIRST_LIST(cfi, next, prev)) {
			counter++;	
		}
		ret = counter;
		break;
	}
	case CBUF_INTRO_FID:  
	{
		for (cfi = FIRST_LIST(&cfi_list, next, prev) ;
		     cfi != &cfi_list;
		     cfi = FIRST_LIST(cfi, next, prev)) {
			nth--;
			if (nth) continue;
			ret = cfi->fid;
			break;
		}
		break;
	}
	case CBUF_INTRO_PAGE: /* find the page for id */
	{
		assert(id > 0);
		d = cos_map_lookup(&cb_ids, id);
		if (!d) goto done;
		
		m = &d->owner;  /* get the current cbuf owner descriptor */
		/* printc("PAGE:id %d owner is %d, we are spd %d\n", id, m->spd, spdid); */
		/* printc("PAGE: d->addr %p, m->addr %p\n", d->addr, m->addr); */

		if (m->spd == spdid) ret = (unsigned int)m->addr;
		goto done;
	}
	case CBUF_INTRO_OWNER: /* find the current owner for id */
	{
		assert(id > 0);
		/* printc("id ---- %d\n", id); */
		d = cos_map_lookup(&cb_ids, id);
		if (!d) goto done;
		
		m = &d->owner;  /* get the current cbuf owner descriptor */
		/* printc("OWNER:id %d owner is %d, we are spd %d\n", id, m->spd, spdid); */
		if (m->spd == spdid) ret = m->spd;
		break;
	}
	default:
		return 0;
	}
done:
	RELEASE();
	return ret;
}


/* Exchange the cbuf descriptor (flags of ownership)
   of old spd and requested spd */

static int 
mgr_update_owner(spdid_t new_spdid, long cbid)
{
	struct spd_tmem_info *old_sti, *new_sti;
	struct cb_desc *d;
	struct cb_mapping *old_owner, *new_owner, tmp;
	union cbuf_meta *mc;
	vaddr_t mgr_addr;

	int ret = 0;
	/* printc("updating cbid %d ownership request spd%d\n", cbid, new_spdid); */
	d = cos_map_lookup(&cb_ids, cbid);
	if (!d) goto err;
	old_owner = &d->owner;
	/* printc("((1)) old_owner->spd %d\n",old_owner->spd); */
	old_sti = get_spd_info(old_owner->spd);
	assert(SPD_IS_MANAGED(old_sti));

	mc = __spd_cbvect_lookup_range(old_sti, cbid);
	if(!mc) goto err;
	/* printc("((2))\n"); */
	if (!CBUF_OWNER(mc->c.flags)) goto err;
	/* printc("((3))\n"); */
	for (new_owner = FIRST_LIST(old_owner, next, prev) ; 
	     new_owner != old_owner; 
	     new_owner = FIRST_LIST(new_owner, next, prev)) {
		if (new_owner->spd == new_spdid) break;
	}

	if (new_owner == old_owner) goto err;
	/* printc("((4))\n"); */
	new_sti = get_spd_info(new_owner->spd);
	assert(SPD_IS_MANAGED(new_sti));

        // this return the whole page for the range
	mgr_addr = __spd_cbvect_retrieve_page(old_sti, cbid); 
	assert(mgr_addr);
	__spd_cbvect_add_range(new_sti, cbid, mgr_addr);

	// exchange the spd and addr in cbuf_mapping
	tmp.spd = old_owner->spd;
	old_owner->spd = new_owner->spd;
	new_owner->spd = tmp.spd;

	tmp.addr = old_owner->addr;
	old_owner->addr = new_owner->addr;
	new_owner->addr = tmp.addr;
	/* printc("((6))\n"); */
	mc->c.flags |= CBUFM_OWNER;
done:
	return ret;
err:
	ret = -1;
	goto done;
}


/* 
   This is called when the component checks if it still owns the cbuf or
   wants to hold a cbuf, if it is not the creater, the ownership
   should be re-granted to it from the original owner. For example,
   when the ramfs server is called and the server wants to keep the 
   cbuf longer before restore.(should also make page read-only? )
*/
/* r_spdid is the requested spd */

int   
cbuf_c_claim(spdid_t r_spdid, int cbid)
{
	assert(cbid >= 0);

	int ret = 0;
	spdid_t o_spdid;
	struct cb_desc *d;

	TAKE();

	d = cos_map_lookup(&cb_ids, cbid);
	if (!d) { 
		ret = -1; 
		printc("can not find cbid %d\n", cbid);
		goto done;
	}

	o_spdid =  d->owner.spd;
	/* printc("o_spd %d r_spd %d\n", o_spdid, r_spdid); */
	if (o_spdid == r_spdid) goto done;

	ret = mgr_update_owner(r_spdid, cbid); // -1 fail, 0 success

	/* FIXME: when does this need to be changed to RW again? */
	if (cos_mmap_cntl(COS_MMAP_RW, COS_MMAP_PFN_READONLY, cos_spd_id(), (vaddr_t)d->addr, 0)) {
		printc("set page to be read only failed\n");
		BUG();
	}

done:
	RELEASE();
	return ret;   
}

void 
cos_init(void *d)
{
	DOUT("CBUFMgr: %d in spd %ld cbuf mgr running.....\n", cos_get_thd_id(), cos_spd_id());
	LOCK_INIT();
	cos_map_init_static(&cb_ids);
	BUG_ON(cos_map_add(&cb_ids, NULL)); /* reserve id 0 */
	int i;

	INIT_LIST(&cfi_list, next, prev); /* for torrent */

	memset(spd_tmem_info_list, 0, sizeof(struct spd_tmem_info) * MAX_NUM_SPDS);
    
	for(i = 0; i < MAX_NUM_SPDS; i++){
		spd_tmem_info_list[i].spdid = i;    
		INIT_LIST(&spd_tmem_info_list[i].ci, next, prev);
		INIT_LIST(&spd_tmem_info_list[i].tmem_list, next, prev);
		INIT_LIST(&spd_tmem_info_list[i].bthd_list, next, prev);
	}

	free_tmem_list = NULL;
	INIT_LIST(&global_blk_list, next, prev);

	tmems_allocated = 0;

	// Map all of the spds we can into this component
	for (i = 0 ; i < MAX_NUM_SPDS ; i++) {
		spdid_t spdid = i;

		void *hp;
		hp = valloc_alloc(cos_spd_id(), cos_spd_id(), 1);
		spdid = cinfo_get_spdid(i);
		if (!spdid) break;

		if(cinfo_map(cos_spd_id(), (vaddr_t)hp, spdid)){
			DOUT("Could not map cinfo page for %d\n", spdid);
			BUG();
		}
		spd_tmem_info_list[spdid].ci.spd_cinfo_page = hp;

		spd_tmem_info_list[spdid].ci.meta = NULL; 
		spd_tmem_info_list[spdid].managed = 1;

		spd_tmem_info_list[spdid].relinquish_mark = 0; 
    
		tmems_target += DEFAULT_TARGET_ALLOC;
		spd_tmem_info_list[spdid].num_allocated = 0;
		spd_tmem_info_list[spdid].num_desired = DEFAULT_TARGET_ALLOC;
		spd_tmem_info_list[spdid].num_blocked_thds = 0;
		spd_tmem_info_list[spdid].num_waiting_thds = 0;
		spd_tmem_info_list[spdid].num_glb_blocked = 0;
		spd_tmem_info_list[spdid].ss_counter = 0;
		spd_tmem_info_list[spdid].ss_max = MAX_NUM_MEM;
		empty_comps++;
	}
	over_quota_total = 0;
	over_quota_limit = MAX_NUM_MEM;

	event_waiting();
	return;
}
 
void 
cbufmgr_buf_report(void)
{
	tmem_report();
	/* int i; */
	/* for (i = 0 ; i < MAX_NUM_SPDS ; i++) { */
	/* 	spdid_t spdid = i; */

	/* 	if (!spd_tmem_info_list[spdid].managed) continue; */

	/* 	printc("spdid %d: allocated %d, desired %d, blked %d, glb_blked %d, ss %d\n", */
	/* 	       spd_tmem_info_list[spdid].spdid, */
	/* 	       spd_tmem_info_list[spdid].num_allocated, */
	/* 	       spd_tmem_info_list[spdid].num_desired, */
	/* 	       spd_tmem_info_list[spdid].num_blocked_thds, */
	/* 	       spd_tmem_info_list[spdid].num_glb_blocked, */
	/* 	       spd_tmem_info_list[spdid].ss_counter); */
	/* } */
}

int
cbufmgr_set_suspension_limit(spdid_t cid, int max)
{
	return tmem_set_suspension_limit(cid, max);
}

int
cbufmgr_detect_suspension(spdid_t cid, int reset)
{
	return tmem_detect_suspension(cid, reset);
}

int
cbufmgr_set_over_quota_limit(int limit)
{
	return tmem_set_over_quota_limit(limit);
}

int
cbufmgr_get_allocated(spdid_t cid)
{
	return tmem_get_allocated(cid);
}

int
cbufmgr_spd_concurrency_estimate(spdid_t spdid)
{
	return tmem_spd_concurrency_estimate(spdid);
}

unsigned long
cbufmgr_thd_blk_time(unsigned short int tid, spdid_t spdid, int reset)
{
	return tmem_get_thd_blk_time(tid, spdid, reset);
}

int
cbufmgr_thd_blk_cnt(unsigned short int tid, spdid_t spdid, int reset)
{
	return tmem_get_thd_blk_cnt(tid, spdid, reset);
}

void
cbufmgr_spd_meas_reset(void)
{
	tmem_spd_meas_reset();
}

int 
cbufmgr_set_concurrency(spdid_t spdid, int concur_lvl, int remove_spare)
{
	return tmem_set_concurrency(spdid, concur_lvl, remove_spare);
}
