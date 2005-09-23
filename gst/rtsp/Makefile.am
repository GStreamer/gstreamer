plugin_LTLIBRARIES = libgstrtsp.la

libgstrtsp_la_SOURCES = gstrtsp.c gstrtspsrc.c \
     			rtspconnection.c	\
     			rtspdefs.c		\
     			rtspmessage.c		\
			rtsptransport.c		\
			rtspurl.c		\
			sdpmessage.c

libgstrtsp_la_CFLAGS = $(GST_CFLAGS)
libgstrtsp_la_LIBADD = $(GST_LIBS)
libgstrtsp_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS)

check_PROGRAMS = test

test_SOURCES = test.c rtspdefs.c rtspurl.c rtspconnection.c rtspmessage.c rtsptransport.c sdpmessage.c
test_CFLAGS = $(GST_CFLAGS)
test_LDFLAGS = $(GST_LIBS)

noinst_HEADERS = gstrtspsrc.h gstrtsp.h rtsptransport.h rtsp.h rtspurl.h rtspconnection.h rtspdefs.h rtspmessage.h sdp.h sdpmessage.h
