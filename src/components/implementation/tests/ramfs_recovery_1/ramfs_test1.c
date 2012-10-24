#include <cos_component.h>
#include <print.h>
#include <sched.h>
#include <cbuf.h>
#include <evt.h>
#include <torrent.h>

#include <ramfs_test2.h>

/* Military Slang: SNAFU, SUSFU, FUBAR, TARFU, BOCHIA */
/* based on the code from unit test for ramfs */

#define VERBOSE 1
#ifdef VERBOSE
#define printv(fmt,...) printc(fmt, ##__VA_ARGS__)
#else
#define printv(fmt,...) 
#endif

/* single thread fails (T writes to foo/bar/who) */
//#define TEST_1		

/* 2 threads, operate on the same files, one fails, (Both writes to
 * foo/bar/who) */
//#define TEST_2			

/* 2 threads, operate on the different files, one fails, (T1 writes
 * foo/bar/who, T2 writes foo/boo/who) */
#define TEST_3			

char buffer[1024];

int high, low;

void test1(void)
{
	td_t t1, t2, t0;
	long evt1, evt2, evt0;
	char *params0, *params1, *params2, *params3;
	char *data0, *data1, *data2, *data3;
	unsigned int ret1, ret2, ret0;
	char *strl, *strh;

	params0 = "who";
	params1 = "bar/";
	params2 = "foo/";
	params3 = "foo/bar/who";
	data0 = "lkevinandy";
	data1 = "l0987654321";
	data2 = "laabbccddeeff";
	data3 = "laabbccddeeffl0987654321";
	strl = "testmore_l";

	printc("\n<<< TEST 1 START (thread %d)>>>\n", cos_get_thd_id());

	evt1 = evt_create(cos_spd_id());
	evt2 = evt_create(cos_spd_id());
	evt0 = evt_create(cos_spd_id());
	assert(evt1 > 0 && evt2 > 0 && evt0 > 0);

	t1 = tsplit(cos_spd_id(), td_root, params2, strlen(params2), TOR_ALL, evt1);
	if (t1 < 1) {
		printc("  split2 failed %d\n", t1); return;
	}

	t2 = tsplit(cos_spd_id(), t1, params1, strlen(params1), TOR_ALL, evt2);
	if (t2 < 1) {
		printc("  split1 failed %d\n", t2); return;
	}

	t0 = tsplit(cos_spd_id(), t2, params0, strlen(params0), TOR_ALL, evt0);
	if (t0 < 1) {
		printc("  split0 failed %d\n", t0); return;
	}

	ret1 = twrite_pack(cos_spd_id(), t1, data1, strlen(data1));
	printc("thread %d writes str %s in %s\n", cos_get_thd_id(), data1, params2);

	ret2 = twrite_pack(cos_spd_id(), t2, data2, strlen(data2));
	printc("thread %d writes str %s in %s%s\n", cos_get_thd_id(), data2, params2, params1);
	
	ret2 = twrite_pack(cos_spd_id(), t2, strl, strlen(strl));
	printc("thread %d writes str %s in %s%s\n", cos_get_thd_id(), strl, params2, params1);

	ret0 = twrite_pack(cos_spd_id(), t0, data0, strlen(data0));
	printc("thread %d writes str %s in %s%s%s\n", cos_get_thd_id(), data0, params2, params1, params0);

	trelease(cos_spd_id(), t1);
	trelease(cos_spd_id(), t2);
	trelease(cos_spd_id(), t0);

	t1 = tsplit(cos_spd_id(), td_root, params2, strlen(params2), TOR_ALL, evt1);
	t2 = tsplit(cos_spd_id(), t1, params1, strlen(params1), TOR_ALL, evt2);
	t0 = tsplit(cos_spd_id(), t2, params0, strlen(params0), TOR_ALL, evt0);
	if (t1 < 1 || t2 < 1 || t0 < 1) {
		printc("later splits failed\n");
		return;
	}

	ret1 = tread_pack(cos_spd_id(), t1, buffer, 1023);
	if (ret1 > 0) buffer[ret1] = '\0';
	printv("thread %d read %d (%d): %s (%s)\n", cos_get_thd_id(),  ret1, strlen(data1), buffer, data1);
	buffer[0] = '\0';
	
	ret2 = tread_pack(cos_spd_id(), t2, buffer, 1023);
	if (ret2 > 0) buffer[ret2] = '\0';
	printv("thread %d read %d: %s\n", cos_get_thd_id(), ret2, buffer);
	buffer[0] = '\0';

	ret0 = tread_pack(cos_spd_id(), t0, buffer, 1023);
	if (ret0 > 0) buffer[ret0] = '\0';
	printv("thread %d read %d: %s\n", cos_get_thd_id(), ret0, buffer);
	buffer[0] = '\0';
		
	trelease(cos_spd_id(), t1);
	trelease(cos_spd_id(), t2);
	trelease(cos_spd_id(), t0);

	printc("<<< TEST 1 PASSED (thread %d)>>>\n\n", cos_get_thd_id());

	return;
}


void test2(void)
{
	td_t t1, t2, t0;
	long evt1, evt2, evt0;
	char *params0, *params1, *params2, *params3;
	char *data0, *data1, *data2, *data3;
	unsigned int ret1, ret2, ret0;
	char *strl, *strh;

	params0 = "who";
	params1 = "bar/";
	params2 = "foo/";
	params3 = "foo/bar/who";
	data0 = "lkevinandy";
	data1 = "l0987654321";
	data2 = "laabbccddeeff";
	data3 = "laabbccddeeffl0987654321";
	strl = "testmore_l";

	printc("\n<<< TEST 2 START (thread %d)>>>\n", cos_get_thd_id());

	evt1 = evt_create(cos_spd_id());
	evt2 = evt_create(cos_spd_id());
	evt0 = evt_create(cos_spd_id());
	assert(evt1 > 0 && evt2 > 0 && evt0 > 0);

	t1 = tsplit(cos_spd_id(), td_root, params2, strlen(params2), TOR_ALL, evt1);
	if (t1 < 1) {
		printc("  split2 failed %d\n", t1); return;
	}

	t2 = tsplit(cos_spd_id(), t1, params1, strlen(params1), TOR_ALL, evt2);
	if (t2 < 1) {
		printc("  split1 failed %d\n", t2); return;
	}

	t0 = tsplit(cos_spd_id(), t2, params0, strlen(params0), TOR_ALL, evt0);
	if (t0 < 1) {
		printc("  split0 failed %d\n", t0); return;
	}

	ret1 = twrite_pack(cos_spd_id(), t1, data1, strlen(data1));
	printc("thread %d writes str %s in %s\n", cos_get_thd_id(), data1, params2);

	ret2 = twrite_pack(cos_spd_id(), t2, data2, strlen(data2));
	printc("thread %d writes str %s in %s%s\n", cos_get_thd_id(), data2, params2, params1);
	
	ret2 = twrite_pack(cos_spd_id(), t2, strl, strlen(strl));
	printc("thread %d writes str %s in %s%s\n", cos_get_thd_id(), strl, params2, params1);

	ret0 = twrite_pack(cos_spd_id(), t0, data0, strlen(data0));
	printc("thread %d writes str %s in %s%s%s\n", cos_get_thd_id(), data0, params2, params1, params0);

	trelease(cos_spd_id(), t1);
	trelease(cos_spd_id(), t2);
	trelease(cos_spd_id(), t0);

	/* let low wake up high here */
	if (cos_get_thd_id() == low) {
		printc("thread %d is going to wakeup thread %d\n", low, high);
		sched_wakeup(cos_spd_id(), high);
	}

	t1 = tsplit(cos_spd_id(), td_root, params2, strlen(params2), TOR_ALL, evt1);
	t2 = tsplit(cos_spd_id(), t1, params1, strlen(params1), TOR_ALL, evt2);
	t0 = tsplit(cos_spd_id(), t2, params0, strlen(params0), TOR_ALL, evt0);
	if (t1 < 1 || t2 < 1 || t0 < 1) {
		printc("later splits failed\n");
		return;
	}

	ret1 = tread_pack(cos_spd_id(), t1, buffer, 1023);
	if (ret1 > 0) buffer[ret1] = '\0';
	printv("thread %d read %d (%d): %s (%s)\n", cos_get_thd_id(),  ret1, strlen(data1), buffer, data1);
	buffer[0] = '\0';
	
	ret2 = tread_pack(cos_spd_id(), t2, buffer, 1023);
	if (ret2 > 0) buffer[ret2] = '\0';
	printv("thread %d read %d: %s\n", cos_get_thd_id(), ret2, buffer);
	buffer[0] = '\0';

	ret0 = tread_pack(cos_spd_id(), t0, buffer, 1023);
	if (ret0 > 0) buffer[ret0] = '\0';
	printv("thread %d read %d: %s\n", cos_get_thd_id(), ret0, buffer);
	buffer[0] = '\0';
		
	trelease(cos_spd_id(), t1);
	trelease(cos_spd_id(), t2);
	trelease(cos_spd_id(), t0);

	printc("<<< TEST 2 PASSED (thread %d)>>>\n\n", cos_get_thd_id());

	return;
}


void test3(void)
{
	td_t t1, t2, t0;
	long evt1, evt2, evt0;
	char *params0, *params1, *params2, *params3;
	char *data0, *data1, *data2, *data3;
	unsigned int ret1, ret2, ret0;
	char *strl, *strh;

	if (cos_get_thd_id() == high) {
		params0 = "who";
		params1 = "bbr/";
		params2 = "foo/";
		params3 = "foo/bbr/who";
		data0 = "handykevin";
		data1 = "h1234567890";
		data2 = "hasdf;lkj";
		data3 = "hasdf;lkjh1234567890";
		strh = "testmore_h";
	}


	if (cos_get_thd_id() == low) {
		params0 = "who";
		params1 = "bar/";
		params2 = "foo/";
		params3 = "foo/bar/who";
		data0 = "lkevinandy";
		data1 = "l0987654321";
		data2 = "laabbccddeeff";
		data3 = "laabbccddeeffl0987654321";
		strl = "testmore_l";
	}

	evt1 = evt_create(cos_spd_id());
	evt2 = evt_create(cos_spd_id());
	evt0 = evt_create(cos_spd_id());
	assert(evt1 > 0 && evt2 > 0 && evt0 > 0);

	t1 = tsplit(cos_spd_id(), td_root, params2, strlen(params2), TOR_ALL, evt1);
	if (t1 < 1) {
		printc("  split2 failed %d\n", t1); return;
	}

	t2 = tsplit(cos_spd_id(), t1, params1, strlen(params1), TOR_ALL, evt2);
	if (t2 < 1) {
		printc("  split1 failed %d\n", t2); return;
	}

	t0 = tsplit(cos_spd_id(), t2, params0, strlen(params0), TOR_ALL, evt0);
	if (t0 < 1) {
		printc("  split0 failed %d\n", t0); return;
	}

	ret1 = twrite_pack(cos_spd_id(), t1, data1, strlen(data1));
	printc("thread %d writes str %s in %s\n", cos_get_thd_id(), data1, params2);

	ret2 = twrite_pack(cos_spd_id(), t2, data2, strlen(data2));
	printc("thread %d writes str %s in %s%s\n", cos_get_thd_id(), data2, params2, params1);
	
	if (cos_get_thd_id() == low) {
		ret2 = twrite_pack(cos_spd_id(), t2, strl, strlen(strl));
		printc("thread %d writes str %s in %s%s\n", cos_get_thd_id(), strl, params2, params1);
	}
	if (cos_get_thd_id() == high) {
		ret2 = twrite_pack(cos_spd_id(), t2, strh, strlen(strh));
		printc("thread %d writes str %s in %s%s\n", cos_get_thd_id(), strh, params2, params1);
	}

	ret0 = twrite_pack(cos_spd_id(), t0, data0, strlen(data0));
	printc("thread %d writes str %s in %s%s%s\n", cos_get_thd_id(), data0, params2, params1, params0);

	trelease(cos_spd_id(), t1);
	trelease(cos_spd_id(), t2);
	trelease(cos_spd_id(), t0);

	/* let low wake up high here */
	if (cos_get_thd_id() == low) {
		printc("thread %d is going to wakeup thread %d\n", low, high);
		sched_wakeup(cos_spd_id(), high);
	}

	ramfs_test2();  	/* all another component to mimic the situation */

	t1 = tsplit(cos_spd_id(), td_root, params2, strlen(params2), TOR_ALL, evt1);
	t2 = tsplit(cos_spd_id(), t1, params1, strlen(params1), TOR_ALL, evt2);
	t0 = tsplit(cos_spd_id(), t2, params0, strlen(params0), TOR_ALL, evt0);
	if (t1 < 1 || t2 < 1 || t0 < 1) {
		printc("later splits failed\n");
		return;
	}

	ret1 = tread_pack(cos_spd_id(), t1, buffer, 1023);
	if (ret1 > 0) buffer[ret1] = '\0';
	printv("thread %d read %d (%d): %s (%s)\n", cos_get_thd_id(),  ret1, strlen(data1), buffer, data1);
	buffer[0] = '\0';
	
	ret2 = tread_pack(cos_spd_id(), t2, buffer, 1023);
	if (ret2 > 0) buffer[ret2] = '\0';
	printv("thread %d read %d: %s\n", cos_get_thd_id(), ret2, buffer);
	buffer[0] = '\0';

	ret0 = tread_pack(cos_spd_id(), t0, buffer, 1023);
	if (ret0 > 0) buffer[ret0] = '\0';
	printv("thread %d read %d: %s\n", cos_get_thd_id(), ret0, buffer);
	buffer[0] = '\0';
		
	trelease(cos_spd_id(), t1);
	trelease(cos_spd_id(), t2);
	trelease(cos_spd_id(), t0);

	return;
}


void cos_init(void)
{
	static int first = 0;
	union sched_param sp;

	if(first == 0){
		first = 1;

		sp.c.type = SCHEDP_PRIO;
		sp.c.value = 10;
		high = sched_create_thd(cos_spd_id(), sp.v, 0, 0);

		sp.c.type = SCHEDP_PRIO;
		sp.c.value = 12;
		low = sched_create_thd(cos_spd_id(), sp.v, 0, 0);

	} else {
		printc("(thread %d in spd %ld)\n", cos_get_thd_id(), cos_spd_id());
#ifdef TEST_1
		if (cos_get_thd_id() == high) test1();
#endif

#ifdef TEST_2
		if (cos_get_thd_id() == high) sched_block(cos_spd_id(), 0);
		test2();
#endif

#ifdef TEST_3
		if (cos_get_thd_id() == high) sched_block(cos_spd_id(), 0);
		test3();
#endif

	}
	
	return;
}
