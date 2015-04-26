
plugin_LTLIBRARIES = libgstmultifile.la

libgstmultifile_la_SOURCES = \
	gstmultifilesink.c   \
	gstmultifilesrc.c    \
	gstmultifile.c       \
	gstsplitfilesrc.c    \
	gstsplitmuxsink.c    \
	gstsplitmuxpartreader.c \
	gstsplitmuxsrc.c \
	gstsplitutils.c    \
	patternspec.c
libgstmultifile_la_CFLAGS = $(GST_PLUGINS_BASE_CFLAGS) $(GST_BASE_CFLAGS) $(GST_CFLAGS) $(GIO_CFLAGS)
libgstmultifile_la_LIBADD = $(GST_PLUGINS_BASE_LIBS) -lgstvideo-@GST_API_VERSION@ $(GST_BASE_LIBS) $(GST_LIBS) $(GIO_LIBS)
libgstmultifile_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS)
libgstmultifile_la_LIBTOOLFLAGS = $(GST_PLUGIN_LIBTOOLFLAGS)

noinst_HEADERS = gstmultifilesrc.h gstmultifilesink.h gstsplitfilesrc.h gstsplitmuxsink.h \
	gstsplitmuxsrc.h gstsplitmuxpartreader.h gstsplitutils.h patternspec.h

noinst_PROGRAMS = test-splitmux-part-reader

test_splitmux_part_reader_SOURCES = \
	test-splitmuxpartreader.c \
	gstsplitmuxpartreader.c \
	gstsplitmuxsrc.c \
	gstsplitutils.c \
	patternspec.c
test_splitmux_part_reader_CFLAGS = $(GST_PLUGINS_BASE_CFLAGS) $(GST_BASE_CFLAGS) $(GST_CFLAGS) $(GIO_CFLAGS)
test_splitmux_part_reader_LDADD = $(GST_PLUGINS_BASE_LIBS) -lgstvideo-@GST_API_VERSION@ $(GST_BASE_LIBS) $(GST_LIBS) $(GIO_LIBS)
test_splitmux_part_reader_LDFLAGS = $(GST_PLUGIN_LDFLAGS)
