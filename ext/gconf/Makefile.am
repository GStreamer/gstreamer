plugin_LTLIBRARIES = libgstgconfelements.la

libgstgconfelements_la_SOURCES = \
	gstgconfaudiosink.c \
	gstgconfaudiosrc.c \
	gstgconfelements.c \
	gstgconfvideosink.c \
	gstgconfvideosrc.c \
	gstswitchsink.c \
	gstswitchsrc.c \
	gstgconf.c

DIR_CFLAGS = -DGST_GCONF_DIR=\"/system/gstreamer/@GST_MAJORMINOR@\"
libgstgconfelements_la_CFLAGS = $(GST_CFLAGS) $(GCONF_CFLAGS) $(DIR_CFLAGS)
libgstgconfelements_la_LIBADD = $(GST_LIBS) $(GCONF_LIBS)
libgstgconfelements_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS)
libgstgconfelements_la_LIBTOOLFLAGS = --tag=disable-static

noinst_HEADERS = \
	gstgconfaudiosink.h \
	gstgconfaudiosrc.h \
	gstgconfelements.h \
	gstgconfvideosink.h \
	gstgconfvideosrc.h \
	gstswitchsink.h \
	gstswitchsrc.h \
	gstgconf.h
