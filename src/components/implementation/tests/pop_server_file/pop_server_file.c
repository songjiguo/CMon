#include <cos_component.h>
#include <print.h>
#include <sched.h>
#include <cbuf.h>
#include <evt.h>
#include <torrent.h>

#include <timed_blk.h>

char buffer[1024];

void pop_cgi(void)
{
	td_t t1, t2;
	long evt1, evt2;
	char *params1 = "test";
	/* char *params2 = "cgi/"; */
	char *data1 = "helloworld", *data2 = "andykevin";
	unsigned int ret1, ret2;

	printc("pop the file on the server\n");
	evt1 = evt_create(cos_spd_id());
	evt2 = evt_create(cos_spd_id());
	assert(evt1 > 0 && evt2 > 0);

	t1 = tsplit(cos_spd_id(), td_root, params1, strlen(params1), TOR_ALL, evt1);

	printc("pop done!!\n");
	/* t1 = tsplit(cos_spd_id(), td_root, params2, strlen(params2), TOR_ALL, evt1); */
	/* t2 = tsplit(cos_spd_id(), t1, params1, strlen(params1), TOR_ALL, evt2); */
	/* if (t1 < 1 || t2 < 1) { */
	/* 	printc("UNIT TEST FAILED: later splits failed\n"); */
	/* 	return; */
	/* } */
	/* ret1 = twrite_pack(cos_spd_id(), t1, data1, strlen(data1)); */
	/* ret2 = twrite_pack(cos_spd_id(), t2, data2, strlen(data2)); */
	return;
}

void cos_init(void)
{
	static int first = 0;
	union sched_param sp;
	
	if(first == 0){
		first = 1;

		sp.c.type = SCHEDP_PRIO;
		sp.c.value = 8;
		sched_create_thd(cos_spd_id(), sp.v, 0, 0);
	} else {
		timed_event_block(cos_spd_id(), 1);
		pop_cgi();
	}
	
	return;
}