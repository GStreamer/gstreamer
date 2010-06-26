plugin_LTLIBRARIES = libgstvideotestsrc.la

ORC_SOURCE=gstvideotestsrcorc
include $(top_srcdir)/common/orc.mak

libgstvideotestsrc_la_SOURCES = \
			gstvideotestsrc.c \
			videotestsrc.c
nodist_libgstvideotestsrc_la_SOURCES = $(ORC_NODIST_SOURCES)

libgstvideotestsrc_la_CFLAGS = $(GST_BASE_CFLAGS) $(GST_CFLAGS) $(ORC_CFLAGS)
libgstvideotestsrc_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS)
libgstvideotestsrc_la_LIBADD = $(GST_BASE_LIBS) $(GST_LIBS) $(ORC_LIBS) $(LIBM)
libgstvideotestsrc_la_LIBTOOLFLAGS = --tag=disable-static

noinst_HEADERS = gstvideotestsrc.h videotestsrc.h


