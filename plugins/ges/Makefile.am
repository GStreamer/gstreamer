plugin_LTLIBRARIES = libgstges.la

libgstges_la_SOURCES = \
	gesplugin.c \
	gessrc.c \
	gesdemux.c

libgstges_la_CFLAGS = -I$(top_srcdir) \
	$(GST_PLUGINS_BASE_CFLAGS) \
	$(GST_BASE_CFLAGS) $(GST_CFLAGS)

libgstges_la_LIBADD = \
	$(top_builddir)/ges/libges-@GST_API_VERSION@.la \
	$(GST_PLUGINS_BASE_LIBS) \
	$(GST_BASE_LIBS) $(GST_LIBS)

libgstges_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS)

noinst_HEADERS = \
	gesdemux.h \
	gessrc.h

