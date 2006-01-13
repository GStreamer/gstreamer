plugin_LTLIBRARIES = libgstcoreindexers.la
# file index uses xml
if HAVE_MMAP
if GST_DISABLE_LOADSAVE
GST_LOADSAVE_SRC =
else
GST_LOADSAVE_SRC = gstfileindex.c
endif
else
GST_LOADSAVE_SRC =
endif

libgstcoreindexers_la_DEPENDENCIES = $(top_builddir)/gst/libgstreamer-@GST_MAJORMINOR@.la
libgstcoreindexers_la_SOURCES = gstindexers.c gstmemindex.c $(GST_LOADSAVE_SRC)
libgstcoreindexers_la_CFLAGS = $(GST_OBJ_CFLAGS)
libgstcoreindexers_la_LIBADD = $(GST_OBJ_LIBS)
libgstcoreindexers_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS)
