plugin_LTLIBRARIES = libgstfrei0r.la

libgstfrei0r_la_SOURCES = \
	gstfrei0r.c \
	gstfrei0rfilter.c \
	gstfrei0rsrc.c \
	gstfrei0rmixer.c

libgstfrei0r_la_CFLAGS = $(GST_PLUGINS_BASE_CFLAGS) $(GST_BASE_CFLAGS) $(GST_CFLAGS)
libgstfrei0r_la_LIBADD = $(GST_PLUGINS_BASE_LIBS) -lgstvideo-@GST_API_VERSION@ \
	$(GST_BASE_LIBS) $(GST_LIBS) $(GMODULE_NO_EXPORT_LIBS)
libgstfrei0r_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS)

noinst_HEADERS = gstfrei0r.h gstfrei0rfilter.h gstfrei0rsrc.h gstfrei0rmixer.h frei0r.h
