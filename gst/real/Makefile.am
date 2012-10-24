plugin_LTLIBRARIES = libgstreal.la 

libgstreal_la_SOURCES =	\
	gstrealvideodec.c \
	gstrealaudiodec.c \
	gstreal.c

libgstreal_la_CFLAGS = \
	$(GST_PLUGINS_BASE_CFLAGS) \
	$(GST_BASE_CFLAGS) \
	$(GST_CFLAGS)
libgstreal_la_LIBADD = \
	$(GST_PLUGINS_BASE_LIBS) -lgstaudio-@GST_API_VERSION@ \
	$(GST_BASE_LIBS)
libgstreal_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS)
libgstreal_la_LIBTOOLFLAGS = $(GST_PLUGIN_LIBTOOLFLAGS)

noinst_HEADERS = 	\
	gstreal.h         \
	gstrealvideodec.h \
	gstrealaudiodec.h
