lib_LTLIBRARIES = libgstcontroller-@GST_MAJORMINOR@.la

libgstcontroller_@GST_MAJORMINOR@_includedir = $(includedir)/gstreamer-@GST_MAJORMINOR@/gst/controller
libgstcontroller_@GST_MAJORMINOR@_include_HEADERS = \
	gstcontroller.h \
	gstcontrolsource.h \
	gstinterpolationcontrolsource.h \
	gstlfocontrolsource.h

noinst_HEADERS = \
	gstcontrollerprivate.h \
	gstinterpolationcontrolsourceprivate.h \
	gstlfocontrolsourceprivate.h

libgstcontroller_@GST_MAJORMINOR@_la_SOURCES = \
	lib.c \
	gstcontroller.c \
	gstinterpolation.c \
	gsthelper.c \
	gstcontrolsource.c \
	gstinterpolationcontrolsource.c \
	gstlfocontrolsource.c

libgstcontroller_@GST_MAJORMINOR@_la_CFLAGS = $(GST_OBJ_CFLAGS)
libgstcontroller_@GST_MAJORMINOR@_la_LIBADD = $(GST_OBJ_LIBS) $(LIBM)
libgstcontroller_@GST_MAJORMINOR@_la_LDFLAGS = $(GST_LIB_LDFLAGS) $(GST_ALL_LDFLAGS) $(GST_LT_LDFLAGS)

CLEANFILES = *.gcno *.gcda *.gcov

%.c.gcov: .libs/libgstcontroller_@GST_MAJORMINOR@_la-%.gcda %.c
	$(GCOV) -b -f -o $^ > $@.out

gcov: $(libgstcontroller_@GST_MAJORMINOR@_la_SOURCES:=.gcov)
