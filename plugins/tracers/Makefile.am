
plugin_LTLIBRARIES = libgstcoretracers.la

if HAVE_GETRUSAGE
RUSAGE_SOURCES = gstrusage.c
else
RUSAGE_SOURCES =
endif

libgstcoretracers_la_DEPENDENCIES = $(top_builddir)/gst/libgstreamer-@GST_API_VERSION@.la
libgstcoretracers_la_SOURCES = \
  gstlog.c \
  $(RUSAGE_SOURCES) \
  gststats.c \
	gsttracers.c

libgstcoretracers_la_CFLAGS = $(GST_OBJ_CFLAGS)
libgstcoretracers_la_LIBADD = \
	$(top_builddir)/gst/printf/libgstprintf.la \
	$(GST_OBJ_LIBS)
libgstcoretracers_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS)
libgstcoretracers_la_LIBTOOLFLAGS = $(GST_PLUGIN_LIBTOOLFLAGS)

noinst_HEADERS = \
  gstlog.h \
  gstrusage.h \
  gststats.h

CLEANFILES = *.gcno *.gcda *.gcov *.gcov.out

%.c.gcov: .libs/libgstcoretracers_la-%.gcda %.c
	$(GCOV) -b -f -o $^ > $@.out

gcov: $(libgstcoretracers_la_SOURCES:=.gcov)

Android.mk: Makefile.am
	androgenizer -:PROJECT gstreamer -:SHARED libgstcoretracers -:TAGS eng debug \
	 -:REL_TOP $(top_srcdir) -:ABS_TOP $(abs_top_srcdir) \
	 -:SOURCES $(libgstcoretracers_la_SOURCES) \
	 -:CFLAGS $(DEFS) $(libgstcoretracers_la_CFLAGS) \
	 -:LDFLAGS $(libgstcoretracers_la_LDFLAGS) \
	            $(libgstcoretracers_la_LIBADD) \
	 -:PASSTHROUGH LOCAL_ARM_MODE:=arm \
	               LOCAL_MODULE_PATH:=$$\(TARGET_OUT\)/lib/gstreamer-@GST_API_VERSION@ \
	> $@
