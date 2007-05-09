plugin_LTLIBRARIES = libgstrtsp.la

libgstrtsp_la_SOURCES = gstrtsp.c gstrtspsrc.c \
			gstrtpdec.c		\
			rtspconnection.c	\
			rtspdefs.c		\
			rtspextwms.c		\
			rtspmessage.c		\
			rtsptransport.c		\
			rtsprange.c		\
			rtspurl.c		\
			sdpmessage.c            \
			base64.c

libgstrtsp_la_CFLAGS = $(GST_PLUGINS_BASE_CFLAGS) $(GST_CFLAGS)
libgstrtsp_la_LIBADD = $(GST_PLUGINS_BASE_LIBS) $(GST_BASE_LIBS) \
		       -lgstrtp-@GST_MAJORMINOR@ $(GST_LIBS) $(WIN32_LIBS)
libgstrtsp_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS)


check_PROGRAMS = test

test_SOURCES = test.c rtspdefs.c rtspurl.c rtspconnection.c rtspmessage.c rtsptransport.c sdpmessage.c base64.c
test_CFLAGS = $(GST_CFLAGS)
test_LDFLAGS = $(GST_LIBS) $(WIN32_LIBS)

noinst_HEADERS = gstrtspsrc.h     \
		 gstrtsp.h        \
		 gstrtpdec.h      \
		 rtsptransport.h  \
		 rtsp.h           \
		 rtspurl.h        \
		 rtsprange.h      \
		 rtspconnection.h \
		 rtspdefs.h       \
		 rtspmessage.h    \
		 sdp.h            \
		 sdpmessage.h     \
		 rtspextwms.h     \
		 rtspext.h        \
		 base64.h
