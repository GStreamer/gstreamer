include ../Rules

tests_pass = thread1 thread2 thread3 thread4 threadf
tests_fail = thread5 threadd queue threadg

tests_ignore = threade threadb threadc

queue_SOURCES = queue.c
queue_CFLAGS = $(AM_CFLAGS)

thread1_SOURCES = thread.c
thread1_CFLAGS = -DTESTNUM=1 $(AM_CFLAGS)
thread2_SOURCES = thread.c
thread2_CFLAGS = -DTESTNUM=2 $(AM_CFLAGS)
thread3_SOURCES = thread.c
thread3_CFLAGS = -DTESTNUM=3 $(AM_CFLAGS)
thread4_SOURCES = thread.c
thread4_CFLAGS = -DTESTNUM=4 $(AM_CFLAGS)
thread5_SOURCES = thread.c
thread5_CFLAGS = -DTESTNUM=5 $(AM_CFLAGS)
