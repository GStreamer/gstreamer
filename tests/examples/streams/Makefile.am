noinst_PROGRAMS = stream-status
if HAVE_PTHREAD
noinst_PROGRAMS += rtpool-test
endif

stream_status_SOURCES = stream-status.c
stream_status_LDADD = $(GST_OBJ_LIBS)
stream_status_CFLAGS = $(GST_OBJ_CFLAGS)

rtpool_test_SOURCES = rtpool-test.c testrtpool.h testrtpool.c
rtpool_test_LDADD = $(GST_OBJ_LIBS) $(PTHREAD_LIBS)
rtpool_test_CFLAGS = $(GST_OBJ_CFLAGS) $(PTHREAD_CFLAGS)

EXTRA_DIST = rtpool-test.c testrtpool.h testrtpool.c
