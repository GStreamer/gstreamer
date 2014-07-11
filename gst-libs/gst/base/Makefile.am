lib_LTLIBRARIES = libgstbadbase-@GST_API_VERSION@.la

libgstbadbase_@GST_API_VERSION@_la_SOURCES = \
	gstaggregator.c

libgstbadbase_@GST_API_VERSION@_la_CFLAGS = $(GST_CFLAGS) \
	-DGST_USE_UNSTABLE_API

libgstbadbase_@GST_API_VERSION@_la_LIBADD = $(GST_LIBS)
libgstbadbase_@GST_API_VERSION@_la_LDFLAGS = $(GST_LIB_LDFLAGS) $(GST_ALL_LDFLAGS) $(GST_LT_LDFLAGS)

noinst_HEADERS =	\
	gstaggregator.h

EXTRA_DIST = 

