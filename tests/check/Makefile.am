TESTS = \
	gst/gstdata	\
	gst/gstobject	\
	gst-libs/gdp

check_PROGRAMS = $(TESTS)

noinst_LTLIBRARIES = libgstcheck.la

libgstcheck_la_SOURCES = gstcheck.c
libgstcheck_la_LIBADD = $(GST_OBJ_LIBS)
noinst_HEADERS = gstcheck.h

AM_CFLAGS = $(GST_OBJ_CFLAGS) $(CHECK_CFLAGS)
LDADD = $(GST_OBJ_LIBS) $(CHECK_LIBS) libgstcheck.la

gst_libs_gdp_SOURCES = \
	gst-libs/gdp.c \
	$(top_srcdir)/libs/gst/dataprotocol/dataprotocol.c
# remove GST_ENABLE_NEW when dataprotocol has been declared API-stable
gst_libs_gdp_CFLAGS = $(AM_CFLAGS) -DGST_ENABLE_NEW
