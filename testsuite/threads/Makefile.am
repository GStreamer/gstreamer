TOP_BUILDDIR=$(shell cd $(top_builddir) && pwd)
TESTS_ENVIRONMENT = GST_PLUGIN_PATH=$(TOP_BUILDDIR) GST_REGISTRY=$(TOP_BUILDDIR)/testsuite/test-registry.xml

testprogs = thread1 thread2 thread3 thread5 threadb
testsfailing = threadc threadd threade threadf thread4

TESTS = $(testprogs)

check_PROGRAMS = $(testprogs)

noinst_PROGRAMS = $(testsfailing)

LDADD = $(GST_LIBS) 
AM_CFLAGS = $(GST_CFLAGS)

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
