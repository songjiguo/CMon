C_OBJS=spin.o
ASM_OBJS=
COMPONENT=cpu.o
INTERFACES=
DEPENDENCIES=periodic_wake lock sched printc mem_mgr timed_blk ll_log  
IF_LIB=

include ../../Makefile.subsubdir
ifeq (${ENABLE_STACK_MANAGER},1)
# $(info Using Simple Stack)
 MANDITORY_LIB=simple_stklib.o
endif

