testprogs = thread1 thread2 thread3 thread4 thread5 threadb

TESTS = $(testprogs)

check_PROGRAMS = $(testprogs)

LDADD = $(GST_LIBS) 
AM_CFLAGS = $(GST_CFLAGS)

thread1_SOURCES = thread.c
thread1_CFLAGS = -DTESTNUM=1
thread2_SOURCES = thread.c
thread2_CFLAGS = -DTESTNUM=2
thread3_SOURCES = thread.c
thread3_CFLAGS = -DTESTNUM=3
thread4_SOURCES = thread.c
thread4_CFLAGS = -DTESTNUM=4
thread5_SOURCES = thread.c
thread5_CFLAGS = -DTESTNUM=5
