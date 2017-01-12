plugin_LTLIBRARIES = libgstttmlsubs.la

# sources used to compile this plug-in
libgstttmlsubs_la_SOURCES = \
	subtitle.c \
	subtitlemeta.c \
	gstttmlparse.c \
	gstttmlparse.h \
	ttmlparse.c \
	ttmlparse.h \
	gstttmlrender.c \
	gstttmlplugin.c

# compiler and linker flags used to compile this plugin, set in configure.ac
libgstttmlsubs_la_CFLAGS = \
	$(GST_PLUGINS_BASE_CFLAGS) \
	$(GST_BASE_CFLAGS) \
	$(GST_CFLAGS) \
	$(TTML_CFLAGS)

libgstttmlsubs_la_LIBADD = \
	$(GST_BASE_LIBS) \
	$(GST_LIBS) \
	-lgstvideo-$(GST_API_VERSION) \
	$(TTML_LIBS) \
	$(LIBM)

libgstttmlsubs_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS)
libgstttmlsubs_la_LIBTOOLFLAGS = $(GST_PLUGIN_LIBTOOLFLAGS)

# headers we need but don't want installed
noinst_HEADERS = \
	subtitle.h \
	subtitlemeta.h \
	gstttmlparse.h \
	ttmlparse.h \
	gstttmlrender.h
