plugin_LTLIBRARIES = libgstfieldanalysis.la

ORC_SOURCE=gstfieldanalysisorc
include $(top_srcdir)/common/orc.mak

libgstfieldanalysis_la_SOURCES = gstfieldanalysis.c gstfieldanalysis.h
nodist_libgstfieldanalysis_la_SOURCES = $(ORC_NODIST_SOURCES)

libgstfieldanalysis_la_CFLAGS = \
	$(GST_PLUGINS_BASE_CFLAGS) \
	$(GST_BASE_CFLAGS) \
	$(GST_CFLAGS) \
	$(ORC_CFLAGS)

libgstfieldanalysis_la_LIBADD = \
	$(GST_PLUGINS_BASE_LIBS) -lgstvideo-@GST_API_VERSION@ \
	$(GST_BASE_LIBS) \
	$(GST_LIBS) \
	$(ORC_LIBS)

libgstfieldanalysis_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS)
libgstfieldanalysis_la_LIBTOOLFLAGS = $(GST_PLUGIN_LIBTOOLFLAGS)

noinst_HEADERS = gstfieldanalysis.h
