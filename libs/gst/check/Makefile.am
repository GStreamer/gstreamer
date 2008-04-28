lib_LTLIBRARIES = libgstcheck-@GST_MAJORMINOR@.la

libgstcheck_@GST_MAJORMINOR@_la_DEPENDENCIES = \
	$(top_builddir)/gst/libgstreamer-@GST_MAJORMINOR@.la
libgstcheck_@GST_MAJORMINOR@_la_SOURCES =	\
	gstbufferstraw.c			\
	gstcheck.c

libgstcheck_@GST_MAJORMINOR@_la_CFLAGS = $(GST_OBJ_CFLAGS) $(CHECK_CFLAGS)
libgstcheck_@GST_MAJORMINOR@_la_LIBADD = $(GST_OBJ_LIBS) $(CHECK_LIBS) $(LIBM)
libgstcheck_@GST_MAJORMINOR@_la_LDFLAGS = $(GST_CHECK_LIB_LDFLAGS) \
        $(GST_ALL_LDFLAGS) $(GST_LT_LDFLAGS)

libgstcheck_@GST_MAJORMINOR@includedir =		\
	$(includedir)/gstreamer-@GST_MAJORMINOR@/gst/check

libgstcheck_@GST_MAJORMINOR@include_HEADERS =	\
	gstbufferstraw.h			\
	gstcheck.h

CLEANFILES = *.gcno *.gcda *.gcov

%.c.gcov: .libs/libgstcheck_@GST_MAJORMINOR@_la-%.gcda %.c
	$(GCOV) -b -f -o $^ > $@.out

gcov: $(libgstcheck_@GST_MAJORMINOR@_la_SOURCES:=.gcov)
