include ../Rules

tests_pass = thread1 thread2 thread3 thread4 thread5 threade threadf signal1 159852 threadg threadi-123775
tests_fail = 159566 signal2 signal3

# threadh

tests_ignore = queue threadb threadc threadd staticrec threadi-nfy1 threadi-nfy2

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

signal1_SOURCES = signals.c
signal1_CFLAGS = -DTESTNUM=1 $(AM_CFLAGS)
signal2_SOURCES = signals.c
signal2_CFLAGS = -DTESTNUM=2 $(AM_CFLAGS)
signal3_SOURCES = signals.c
signal3_CFLAGS = -DTESTNUM=3 $(AM_CFLAGS)

threadi_123775_SOURCES = threadi.c
threadi_123775_CFLAGS = -DTESTNUM=123775 $(AM_CFLAGS)
threadi_nfy1_SOURCES = threadi.c
threadi_nfy1_CFLAGS = -DTESTNUM=999998 $(AM_CFLAGS)
threadi_nfy2_SOURCES = threadi.c
threadi_nfy2_CFLAGS = -DTESTNUM=999999 $(AM_CFLAGS)
