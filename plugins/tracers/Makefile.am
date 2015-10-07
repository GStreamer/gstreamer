
plugin_LTLIBRARIES = libgstcoretracers.la

if HAVE_RUSAGE_THREAD
RUSAGE_SOURCES = gstrusage.c
else
RUSAGE_SOURCES =
endif

if GST_DISABLE_GST_DEBUG
LOG_SOURCES =
else
LOG_SOURCES = gstlog.c
endif

libgstcoretracers_la_DEPENDENCIES = $(top_builddir)/gst/libgstreamer-@GST_API_VERSION@.la
libgstcoretracers_la_SOURCES = \
  gstlatency.c \
  $(LOG_SOURCES) \
  $(RUSAGE_SOURCES) \
  gststats.c \
	gsttracers.c

libgstcoretracers_la_CFLAGS = $(GST_OBJ_CFLAGS) \
  -DGST_USE_UNSTABLE_API
libgstcoretracers_la_LIBADD = \
	$(top_builddir)/gst/printf/libgstprintf.la \
	$(GST_OBJ_LIBS)
libgstcoretracers_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS)
libgstcoretracers_la_LIBTOOLFLAGS = $(GST_PLUGIN_LIBTOOLFLAGS)

noinst_HEADERS = \
  gstlatency.h \
  gstlog.h \
  gstrusage.h \
  gststats.h

CLEANFILES = *.gcno *.gcda *.gcov *.gcov.out

%.c.gcov: .libs/libgstcoretracers_la-%.gcda %.c
	$(GCOV) -b -f -o $^ > $@.out

gcov: $(libgstcoretracers_la_SOURCES:=.gcov)

