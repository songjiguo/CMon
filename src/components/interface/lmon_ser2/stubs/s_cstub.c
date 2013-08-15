#include <cos_component.h>
#include <print.h>

#include <lmon_ser2.h>

#include <ll_log.h>

vaddr_t __sg_lmon_ser2_test(spdid_t spdid, int event_id)
{
	vaddr_t ret = 0;

	monevt_enqueue(cos_spd_id());
	ret = lmon_ser2_test();
	monevt_enqueue(0);

	return ret;
}



