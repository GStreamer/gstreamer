plugin_LTLIBRARIES = libgstgconfelements.la

libgstgconfelements_la_SOURCES = \
	gstgconfaudiosink.c \
	gstgconfelements.c \
	gstgconfvideosink.c \
	gconf.c

DIR_CFLAGS = -DGST_GCONF_DIR=\"/system/gstreamer/@GST_MAJORMINOR@\"
libgstgconfelements_la_CFLAGS = $(GST_CFLAGS) $(GCONF_CFLAGS) $(DIR_CFLAGS)
libgstgconfelements_la_LIBADD = $(GCONF_LIBS)
libgstgconfelements_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS)

noinst_HEADERS = \
	gstgconfaudiosink.h \
	gstgconfelements.h \
	gstgconfvideosink.h \
	gconf.h
