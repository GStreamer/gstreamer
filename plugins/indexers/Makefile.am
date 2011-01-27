plugin_LTLIBRARIES = libgstcoreindexers.la

# file index uses xml
if HAVE_MMAP
if GST_DISABLE_LOADSAVE
GST_LOADSAVE_SRC =
GST_FILEINDEX_LIBS =
else
GST_LOADSAVE_SRC = gstfileindex.c
GST_FILEINDEX_LIBS = $(XML_LIBS)
endif
else
GST_LOADSAVE_SRC =
GST_FILEINDEX_LIBS =
endif

noinst_HEADERS =		\
	gstindexers.h

libgstcoreindexers_la_DEPENDENCIES = $(top_builddir)/gst/libgstreamer-@GST_MAJORMINOR@.la
libgstcoreindexers_la_SOURCES = gstindexers.c gstmemindex.c $(GST_LOADSAVE_SRC)
libgstcoreindexers_la_CFLAGS = $(GST_OBJ_CFLAGS)
libgstcoreindexers_la_LIBADD = $(GST_OBJ_LIBS) $(GST_FILEINDEX_LIBS)
libgstcoreindexers_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS)
libgstcoreindexers_la_LIBTOOLFLAGS = --tag=disable-static

%.c.gcov: .libs/libgstcoreindexers_la-%.gcda %.c
	$(GCOV) -b -f -o $^ > $@.out

gcov: $(libgstcoreindexers_la_SOURCES:=.gcov)

Android.mk: Makefile.am
	androgenizer -:PROJECT gstreamer -:SHARED libgstcoreindexers -:TAGS eng debug \
	 -:REL_TOP $(top_srcdir) -:ABS_TOP $(abs_top_srcdir) \
	 -:SOURCES $(libgstcoreindexers_la_SOURCES) \
	 -:CFLAGS $(DEFS) $(libgstcoreindexers_la_CFLAGS) \
	 -:LDFLAGS $(libgstcoreindexers_la_LDFLAGS) \
	           $(libgstcoreindexers_la_LIBADD) \
	 -:PASSTHROUGH LOCAL_ARM_MODE:=arm \
	               LOCAL_MODULE_PATH:=$$\(TARGET_OUT\)/lib/gstreamer-@GST_MAJORMINOR@ \
	> $@

